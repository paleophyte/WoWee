// Optional, bounded integration executable. Uses the client's actual startup,
// update and render methods; no substitute world loader or transport manager.
#include "core/application.hpp"
#include "core/config_paths.hpp"
#include "core/input.hpp"
#include "core/logger.hpp"
#include "core/window.hpp"
#include "rendering/renderer.hpp"
#include "rendering/vk_context.hpp"
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace wowee::game {
struct GameHandlerTestAccess {
    static void prepareFrame(core::Application& app,float deltaTime){
        core::Input::getInstance().update();app.update(deltaTime);
    }
    static void frame(core::Application& app, float deltaTime) {
        core::Input::getInstance().update();
        app.update(deltaTime);
        app.render();
    }
};
}

#include "scene_terrain_probe.hpp"

int main(int argc, char** argv) {
    using json = nlohmann::json;
    if (argc!=2) { std::cerr << "Usage: wowee_render_probe TRACE.jsonl < private-config.json\n"; return 2; }
    std::ofstream trace;
    try {
        if (std::filesystem::exists(argv[1])) throw std::runtime_error("Trace already exists");
        std::string line;
        if (!std::getline(std::cin,line)) throw std::runtime_error("Configuration missing");
        const auto config=json::parse(line);
        const auto mode=config.value("mode",std::string{});
        if (config.value("scope",std::string{})!="isolated_tbc_qa" ||
            (mode!="renderer_smoke" && mode!="scene_terrain"))
            throw std::runtime_error("Only isolated renderer modes are implemented");
        const char* root=std::getenv("WOWEE_CONFIG_ROOT");
        const char* data=std::getenv("WOW_DATA_PATH");
        const char* hidden=std::getenv("WOWEE_WINDOW_HIDDEN");
        if (!root || !*root || !data || !*data || !hidden || std::string(hidden)!="1")
            throw std::runtime_error("Explicit isolated config/data roots and hidden window required");
        const auto marker=std::filesystem::path(root).parent_path()/"isolated-render-probe.json";
        if (!std::filesystem::is_regular_file(marker))
            throw std::runtime_error("Isolated preparation marker missing");
        std::ifstream markerFile(marker);
        const auto prepared=json::parse(markerFile);
        if (std::filesystem::weakly_canonical(data)!=std::filesystem::weakly_canonical(prepared.at("data_root").get<std::string>()) ||
            std::filesystem::weakly_canonical(root)!=std::filesystem::weakly_canonical(prepared.at("config_root").get<std::string>()))
            throw std::runtime_error("Prepared roots differ");
        trace.open(argv[1]);
        auto record=[&](const char* event,json values=json::object()) {
            values["event"]=event;trace << values.dump() << std::endl;
        };
        record("starting",{{"mode",mode},{"game_connections_requested",0}});
        wowee::core::Application app;
        if (!app.initialize()) throw std::runtime_error("Normal Application::initialize failed");
        auto* window=app.getWindow();auto* renderer=app.getRenderer();
        if (!window || !renderer || !renderer->getVkContext() || !app.getEntitySpawner() || !app.getWorldLoader())
            throw std::runtime_error("Normal renderer/world-loader/spawner dependencies missing");
        if (!(SDL_GetWindowFlags(window->getSDLWindow()) & SDL_WINDOW_HIDDEN))
            throw std::runtime_error("Integration window became visible");
        record("initialized",{{"gpu",renderer->getVkContext()->getGpuName()},
               {"hidden",true},{"entity_spawner",true},{"world_loader",true},
               {"loaded_map",app.getWorldLoader()->getLoadedMapId()}});
        if(mode=="scene_terrain"){
            sceneTerrainProbe(app,std::filesystem::path(argv[1]),record);
            app.shutdown();
            record("complete",{{"state","pass"},{"game_connections_requested",0},
                {"scope","Offline production scene rendering and real pending-work resets; no authenticated world entry or gameplay acceptance"}});
            return 0;
        }
        for (int i=0;i<3;++i) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type==SDL_EVENT_QUIT) throw std::runtime_error("Window closed during smoke run");
            }
            wowee::game::GameHandlerTestAccess::frame(app,.016f);
            record("rendered_frame",{{"sequence",i+1}});
        }
        const auto image=std::filesystem::path(argv[1]).replace_extension(".png");
        if (!renderer->captureScreenshot(image.string())) throw std::runtime_error("Rendered capture failed");
        record("screenshot",{{"path",image.string()}});
        app.shutdown();
        record("complete",{{"state","pass"},{"frames",3},{"game_connections_requested",0},
               {"scope","Real renderer initialization only; no ferry, map load, login or gameplay acceptance"}});
        return 0;
    } catch (const std::exception& error) {
        if (trace.is_open()) trace << json({{"event","failure"},{"message",error.what()}}).dump() << std::endl;
        std::cerr << "Rendered probe failed: " << error.what() << '\n';
        return 1;
    }
}
