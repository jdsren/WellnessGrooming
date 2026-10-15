#include "WG.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace {
    constexpr std::uint32_t kUniqueID = 'WGRM';

    void SetupLog() {
        auto path = SKSE::log::log_directory();
        if (!path) return;
        *path /= "WellnessGrooming.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S] [%l] %v");
    }

    void OnMessage(SKSE::MessagingInterface::Message* msg) {
        using M = SKSE::MessagingInterface;
        switch (msg->type) {
        case M::kDataLoaded:
            WG::Install();
            WG::StartTickThread();
            break;
        case M::kPostLoadGame:
            WG::OnLoaded(false);
            break;
        case M::kNewGame:
            WG::OnLoaded(true);
            break;
        default: break;
        }
    }
}

SKSEPluginInfo(.Version = REL::Version{ 1, 0, 0, 0 }, .Name = "WellnessGrooming", .Author = "built for Wellness")

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SetupLog();
    SKSE::Init(skse);
    logger::info("WellnessGrooming loading");
    auto* ser = SKSE::GetSerializationInterface();
    ser->SetUniqueID(kUniqueID);
    ser->SetSaveCallback([](SKSE::SerializationInterface* i) { WG::SaveTo(i); });
    ser->SetLoadCallback([](SKSE::SerializationInterface* i) { WG::LoadFrom(i); });
    ser->SetRevertCallback([](SKSE::SerializationInterface*) { WG::Revert(); });
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
