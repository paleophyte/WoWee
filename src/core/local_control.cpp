#include "core/local_control.hpp"
#include <stdexcept>
#include <utility>
#if defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <vector>
namespace wowee::core {
struct LocalControlServer::Impl {
    struct Client {
        int fd;
        std::string input, output, id, command;
        size_t written=0;
        bool pending=false;
        Clock::time_point deadline;
    };
    int listener=-1;
    std::string path;
    dev_t device{}; ino_t inode{};
    Handler handler;
    std::vector<Client> clients;
    ~Impl() {
        for (const auto& c:clients) close(c.fd);
        if (listener>=0) close(listener);
        struct stat info{};
        if (!path.empty() && lstat(path.c_str(),&info)==0 && info.st_dev==device && info.st_ino==inode)
            unlink(path.c_str());
    }
    void respond(Client& c, const std::string& outcome, Json control=Json::object()) {
        c.pending=false;
        c.output=Json{{"version",1},{"id",c.id},{"command",c.command},{"outcome",outcome},{"control",control}}.dump()+"\n";
        if (c.output.size()>16384) c.output=Json{{"version",1},{"id",c.id},{"outcome","internal_error"}}.dump()+"\n";
    }
    void cancel(Client& c) { if (c.pending) { handler("cancel",c.id); c.pending=false; } }
};
static void nonblocking(int fd) {
    if (fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK)<0 || fcntl(fd,F_SETFD,FD_CLOEXEC)<0)
        throw std::runtime_error("Cannot configure local control socket");
#ifdef __APPLE__
    int one=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));
#endif
}
LocalControlServer::LocalControlServer(const std::string& path, Handler handler):impl_(std::make_unique<Impl>()) {
    namespace fs=std::filesystem;
    const fs::path parent=fs::path(path).parent_path();
    struct stat owner{};
    if (!fs::path(path).is_absolute() || fs::canonical(parent)!=parent ||
        lstat(parent.c_str(),&owner)!=0 || !S_ISDIR(owner.st_mode) ||
        owner.st_uid!=getuid() || (owner.st_mode&077)!=0)
        throw std::runtime_error("Control socket requires an absolute path in an owned private directory (mode0700)");
    sockaddr_un address{};address.sun_family=AF_UNIX;
    if (path.size()>=sizeof(address.sun_path)) throw std::runtime_error("Control socket path is too long");
    std::memcpy(address.sun_path,path.c_str(),path.size()+1);
    impl_->handler=std::move(handler);
    impl_->listener=socket(AF_UNIX,SOCK_STREAM,0);
    if (impl_->listener<0) throw std::runtime_error("Cannot create local control socket");
    nonblocking(impl_->listener);
    if (bind(impl_->listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0)
        throw std::runtime_error("Cannot bind control socket; existing endpoints are never removed");
    struct stat info{};
    if (lstat(path.c_str(),&info)!=0) throw std::runtime_error("Cannot inspect bound socket");
    impl_->path=path;impl_->device=info.st_dev;impl_->inode=info.st_ino;
    if (chmod(path.c_str(),0600)!=0 || listen(impl_->listener,8)!=0)
        throw std::runtime_error("Cannot secure/listen on local control socket");
}
LocalControlServer::~LocalControlServer()=default;
void LocalControlServer::tick(Clock::time_point now) {
    auto& s=*impl_;
    for (int n=0;n<4 && s.clients.size()<8;++n) {
        int fd=accept(s.listener,nullptr,nullptr);
        if (fd<0) break;
#ifdef __APPLE__
        uid_t peerUid; gid_t peerGid;
        if (getpeereid(fd,&peerUid,&peerGid)!=0 || peerUid!=getuid()) { close(fd);continue; }
#else
        struct ucred peer{};socklen_t size=sizeof(peer);
        if (getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&size)!=0 || peer.uid!=getuid()) { close(fd);continue; }
#endif
        try { nonblocking(fd); } catch (...) { close(fd);continue; }
        s.clients.push_back({fd,{},{},{},{},0,false,now+std::chrono::seconds(5)});
    }
    for (auto it=s.clients.begin();it!=s.clients.end();) {
        auto& c=*it;bool done=false;
        if (c.output.empty()) {
            char buffer[1024];
            const auto count=recv(c.fd,buffer,sizeof(buffer),0);
            if (count==0 || (count<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) done=true;
            else if (count>0) {
                if (c.pending) { s.cancel(c);s.respond(c,"invalid_request"); }
                else {
                    c.input.append(buffer,static_cast<size_t>(count));
                    if (c.input.size()>4096) s.respond(c,"invalid_request");
                    else if (c.input.find('\n')!=std::string::npos) {
                        try {
                            auto request=Json::parse(c.input);
                            if (!request.is_object() || !request.contains("id") || !request.contains("command") ||
                                request.size()>(request.contains("timeout_seconds")?3u:2u)) throw std::runtime_error("Invalid fields");
                            c.id=request.at("id").get<std::string>();c.command=request.at("command").get<std::string>();
                            if (c.id.empty() || c.id.size()>64 || c.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)
                                throw std::runtime_error("Invalid id");
                            if (c.command!="status" && c.command!="manual" && c.command!="bot" && c.command!="stop") throw std::runtime_error("Invalid command");
                            if (request.contains("timeout_seconds") && !request["timeout_seconds"].is_number_integer()) throw std::runtime_error("Invalid timeout");
                            const int timeout=request.value("timeout_seconds",15);
                            if (timeout<1 || timeout>30) throw std::runtime_error("Invalid timeout");
                            c.deadline=now+std::chrono::seconds(timeout);
                            auto result=s.handler(c.command,c.id);
                            if (c.command=="status") s.respond(c,"observed",result);
                            else if (result.value("outcome","")=="requested") c.pending=true;
                            else s.respond(c,result.value("outcome","unavailable"),result);
                        } catch (...) { s.respond(c,"invalid_request"); }
                    }
                }
            }
            if (c.pending && !done) {
                auto result=s.handler("poll",c.id);
                if (result.value("request_id","")!=c.id) s.respond(c,"superseded",result);
                else if (result.value("outcome","requested")!="requested") s.respond(c,result.value("outcome","unavailable"),result);
            }
            if (c.output.empty() && now>=c.deadline) {
                s.cancel(c);
                s.respond(c,"timed_out",{{"control_may_still_be_active",true}});
            }
        }
        if (done) s.cancel(c);
        if (!done && !c.output.empty()) {
#ifdef MSG_NOSIGNAL
            constexpr int flags=MSG_NOSIGNAL;
#else
            constexpr int flags=0;
#endif
            auto sent=send(c.fd,c.output.data()+c.written,c.output.size()-c.written,flags);
            if (sent>0) c.written+=static_cast<size_t>(sent);
            else if (sent<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) done=true;
            if (c.written==c.output.size()) done=true;
        }
        if (done) { close(c.fd);it=s.clients.erase(it); } else ++it;
    }
}
}
#else
namespace wowee::core {
struct LocalControlServer::Impl {};
LocalControlServer::LocalControlServer(const std::string&,Handler) { throw std::runtime_error("Local control sockets currently require desktop macOS or Linux"); }
LocalControlServer::~LocalControlServer()=default;
void LocalControlServer::tick(Clock::time_point) {}
}
#endif
