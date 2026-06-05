#include "PCH.h"
#include "logger.h"
#include "ryml/ryml.hpp"
#include "c4/substr.hpp"
#include "c4/std/string.hpp"
#include <charconv>
#include <utility>
#include <map>
#include <set>

std::map<std::string, spdlog::level::level_enum> logLevelMap = {
    {"trace", spdlog::level::level_enum::trace},
    {"debug", spdlog::level::level_enum::debug},
    {"info", spdlog::level::level_enum::info},
    {"warn", spdlog::level::level_enum::warn},
    {"warning", spdlog::level::level_enum::warn},
    {"err", spdlog::level::level_enum::err},
    {"error", spdlog::level::level_enum::err},
    {"critical", spdlog::level::level_enum::critical},
    {"crit", spdlog::level::level_enum::critical},
    {"off", spdlog::level::level_enum::off},
};
std::map<spdlog::level::level_enum, std::string> logLevelList = {
    {spdlog::level::level_enum::trace, "trace"},
    {spdlog::level::level_enum::debug, "debug"},
    {spdlog::level::level_enum::info, "info"},
    {spdlog::level::level_enum::warn, "warning"},
    {spdlog::level::level_enum::err, "error"},
    {spdlog::level::level_enum::critical, "critical"},
    {spdlog::level::level_enum::off, "off"}
};

std::uint32_t parseHex(std::string_view s) {
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s.remove_prefix(2);
    }
    std::uint32_t value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value, 16);
    if (ec != std::errc{} || ptr != s.data() + s.size()) {
        throw std::runtime_error(fmt::format("Invalid hex formId: {}", s));
    }
    return value;
}
std::string yamlNodeToString(const c4::yml::NodeRef& node) {
    auto buf = node.val();
    return std::string{buf.data(), buf.size()};
}
std::string yamlNodeToString(const c4::yml::ConstNodeRef& node) {
    auto buf = node.val();
    return std::string{buf.data(), buf.size()};
}

class Config {
public:
    [[nodiscard]] spdlog::level::level_enum getLogLevel() const {
        return logLevel;
    }
    static const Config& getSingleton() noexcept {
        static Config instance;

        static std::atomic_bool initialized;
        if (!initialized.exchange(true)) {
            std::ifstream inputFile(R"(Data\SKSE\Plugins\IdrinthOutfitSystem.yaml)", std::ios::in);
            if (!inputFile.good()) {
                return instance;
            }
            std::string data;
            for (std::string line; std::getline(inputFile, line); ) {
                data.append(line);
                data.append("\n");
            }
            inputFile.close();

            c4::substr sus = c4::to_substr(data);
            c4::yml::Tree tree = ryml::parse_in_place(sus);
            const c4::yml::NodeRef node = tree["logLevel"];
            if (node.invalid() || node.val_is_null()) {
                return instance;
            }
            if (const std::string out = yamlNodeToString(node); logLevelMap.contains(out)) {
                instance.logLevel = logLevelMap[out];
            }
        }

        return instance;
    }
private:
    spdlog::level::level_enum logLevel = spdlog::level::level_enum::err;
};
class TrackedArmorPair {
    public:
        TrackedArmorPair(RE::TESObjectARMO* a_military, RE::TESObjectARMO* a_civilian, bool a_provideCivilian = true, bool a_provideMilitary = true)
            : military(a_military), civilian(a_civilian), provideMilitary(a_provideMilitary), provideCivilian(a_provideCivilian) {}
        RE::TESObjectARMO* military;
        RE::TESObjectARMO* civilian;
        bool provideMilitary;
        bool provideCivilian;
};
class TrackedNPC {
    public:
        TrackedNPC(
            const RE::Actor* actor,
            const std::list<std::string>& ignoredEditorIDs,
            const std::list<std::string>& civilianKeywordList,
            const std::list<TrackedArmorPair>& outfit,
            RE::Effect* undressEffect,
            std::list<RE::TESFaction*> undressFactions
        ) {
            civilianKeywords = civilianKeywordList;
            factionsOSA = std::move(undressFactions);
            if (!actor->HasContainer()) {
                throw std::runtime_error("No inventory for npc.");
            }
            gear = outfit;
            magicEffectBathUndress = undressEffect;
            ignoredArmorEditorIDs = ignoredEditorIDs;
        }
        static bool isInList(const std::string& editorId, const std::list<std::string>& list) {
            for (auto& item : list) {
                if (item == editorId) {
                    return true;
                }
            }
            return false;
        }
        void handleUnequip(RE::Actor* npc, const std::set<RE::TESBoundObject*>& worn) const {
            auto* eqMgr = RE::ActorEquipManager::GetSingleton();
            for (const auto& armorPair : gear) {
                if (!armorPair.military || !armorPair.civilian) {
                    continue;
                }
                if (worn.contains(armorPair.military)) {
                    eqMgr->UnequipObject(npc, armorPair.military, nullptr, 1, nullptr, true, false, false, false, nullptr);
                }
                if (worn.contains(armorPair.civilian)) {
                    eqMgr->UnequipObject(npc, armorPair.civilian, nullptr, 1, nullptr, true, false, false, false, nullptr);
                }
            }
        }
        void handleEquip(RE::Actor* npc) const {
            if (processing.exchange(true)) {
                return;
            }
            struct Guard { std::atomic_bool& f; ~Guard(){ f = false; } } guard{processing};

            if (!npc) {
                return;
            }
            logger::debug("Equipping NPC {}", npc->GetName());

            if (npc->IsDead() || npc->IsDisabled() || !npc->Is3DLoaded()) {
                logger::debug("NPC {} is inactive(dead, disabled or not loaded)", npc->GetName());
                return;
            }
            if (!npc->HasContainer()) {
                logger::debug("NPC {} has no inventory", npc->GetName());
                return;
            }

            auto* invChanges = npc->GetInventoryChanges();
            if (!invChanges || !invChanges->entryList) {
                logger::debug("NPC {} inventory not ready yet", npc->GetName());
                return;
            }

            const auto counts = npc->GetInventoryCounts();
            std::set<RE::TESBoundObject*> worn;
            for (auto* entry : *invChanges->entryList) {
                if (!entry || !entry->object || !entry->extraLists) {
                    continue;
                }
                for (auto* xList : *entry->extraLists) {
                    if (xList && xList->HasType(RE::ExtraDataType::kWorn)) {
                        worn.insert(entry->object);
                        break;
                    }
                }
            }
            const auto hasItem = [&counts](RE::TESBoundObject* obj) -> bool {
                if (!obj) {
                    return false;
                }
                const auto it = counts.find(obj);
                return it != counts.end() && it->second > 0;
            };
            const auto isWorn = [&worn](RE::TESBoundObject* obj) -> bool {
                return obj && worn.contains(obj);
            };
            logger::trace("Inventory snapshot built: {} distinct objects, {} worn", counts.size(), worn.size());

            if (npc->GetActorBase() && (npc->GetActorBase()->defaultOutfit || npc->GetActorBase()->sleepOutfit)) {
                logger::debug("NPC {} is uninitialized, handling now", npc->GetName());
                npc->SetDefaultOutfit(nullptr, false);
                npc->SetSleepOutfit(nullptr, false);
                for (const auto& armorPair : gear) {
                    const auto military = armorPair.military;
                    const auto civilian = armorPair.civilian;
                    if (!military || !civilian) {
                        continue;
                    }
                    if (armorPair.provideMilitary && !isInList(military->GetFormEditorID(), ignoredArmorEditorIDs) && !hasItem(military)) {
                        npc->AddObjectToContainer(military, nullptr, 1, nullptr);
                    }
                    if (armorPair.provideCivilian && military != civilian && !isInList(civilian->GetFormEditorID(), ignoredArmorEditorIDs) && !hasItem(civilian)) {
                        npc->AddObjectToContainer(civilian, nullptr, 1, nullptr);
                    }
                }
                logger::debug("NPC {} initialised, deferring equip to next tick", npc->GetName());
                return;
            }
            if (magicEffectBathUndress && !npc->IsInCombat() && npc->AsMagicTarget() && npc->AsMagicTarget()->HasMagicEffect(magicEffectBathUndress->baseEffect)) {
                handleUnequip(npc, worn);
                logger::debug("Unequipping NPC {} due to magic effect done", npc->GetName());
                return;
            }
            for (const auto& faction : factionsOSA) {
                if (npc->IsInFaction(faction)) {
                    handleUnequip(npc, worn);
                    logger::debug("Unequipping NPC {} due to factions done", npc->GetName());
                    return;
                }
            }
            const bool isCivilian = !npc->IsInCombat() && isInCivilianLocation(npc->GetCurrentLocation());
            for (const auto& armorPair : gear) {
                const auto preferred = isCivilian ? armorPair.civilian : armorPair.military;
                const auto fallback = isCivilian ? armorPair.military : armorPair.civilian;
                if (!preferred) {
                    logger::error("Preferred is nullptr, how did that happen?");
                    continue;
                }
                if (!fallback) {
                    logger::error("Fallback is nullptr, how did that happen?");
                    continue;
                }
                logger::trace("Evaluating pair preferred={:08X} '{}' fallback={:08X} '{}'",
                    preferred->GetFormID(), preferred->GetName(),
                    fallback->GetFormID(), fallback->GetName());
                if (!isWorn(preferred)) {
                    if (hasItem(preferred)) {
                        RE::ActorEquipManager::GetSingleton()->EquipObject(npc, preferred, nullptr, 1, nullptr, true, false, false, false);
                        logger::debug("Equipped preferred gear: {}", preferred->GetName());
                    } else if (!isWorn(fallback) && hasItem(fallback)) {
                        RE::ActorEquipManager::GetSingleton()->EquipObject(npc, fallback, nullptr, 1, nullptr, true, false, false, false);
                        logger::debug("Equipped fallback gear {} instead of {}", fallback->GetName(), preferred->GetName());
                    } else if (isWorn(fallback)) {
                        logger::debug("Already equipped fallback gear: {}", fallback->GetName());
                    } else {
                        logger::debug("Neither fallback gear {} nor preferred gear {} available", fallback->GetName(), preferred->GetName());
                    }
                } else {
                    logger::debug("Already equipped preferred gear: {}", preferred->GetName());
                }
            }
            logger::debug("Equipped NPC {}", npc->GetName());
        }
    private:
        bool isInCivilianLocation(const RE::BGSLocation* location) const {
            if (!location) {
                return false;
            }
            for (const auto& keyword : civilianKeywords) {
                if (location->HasKeywordString(keyword)) {
                    return true;
                }
            }
            return isInCivilianLocation(location->parentLoc);
        }
        std::list<TrackedArmorPair> gear;
        std::list<std::string> civilianKeywords;
        RE::Effect* magicEffectBathUndress;
        std::list<RE::TESFaction*> factionsOSA;
        std::list<std::string> ignoredArmorEditorIDs;
        mutable std::atomic_bool processing{false};
};

class EquipmentEventSink: public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>, public RE::BSTEventSink<RE::TESCombatEvent>, public RE::BSTEventSink<RE::TESEquipEvent>, public RE::BSTEventSink<RE::TESCellAttachDetachEvent>, public RE::BSTEventSink<RE::TESActorLocationChangeEvent>, public RE::BSTEventSink<RE::TESObjectLoadedEvent> {
    public:
        EquipmentEventSink() = default;
        EquipmentEventSink(const EquipmentEventSink&) = delete;
        EquipmentEventSink(EquipmentEventSink&&) = delete;
        EquipmentEventSink& operator=(const EquipmentEventSink&) = delete;
        EquipmentEventSink& operator=(const EquipmentEventSink&&) = delete;

        static EquipmentEventSink* getSingleton()
        {
            static EquipmentEventSink instance;
            return &instance;
        }
        void handleNPC(const c4::yml::ConstNodeRef child, const std::list<std::string>& civilianKeywords, RE::Effect* magicEffect, const std::list<RE::TESFaction*>& factions) {
            std::string modName = yamlNodeToString(child["modName"]);
            std::string formId = yamlNodeToString(child["formId"]);
            const std::uint32_t fid = parseHex(formId);
            if (fid == 0) {
                logger::error("Invalid formId for NPC: {} of {}", formId, modName);
                return;
            }
            const auto form = RE::TESDataHandler::GetSingleton()->LookupForm(fid, modName);
            if (!form) {
                logger::warn("Failed to locate NPC: {} of {}", formId, modName);
                return;
            }
            const auto npc = form->As<RE::Actor>();
            if (!npc) {
                logger::error("Failed to locate NPC: {} of {} - not an Actor", formId, modName);
                return;
            }
            if (npcs.contains(npc->GetFormID())) {
                npcs.erase(npc->GetFormID());
            }
            std::list<std::string> ignoredEditorIDs;
            const auto node2 = child["ignoredEditorIDs"];
            if (!node2.invalid() && node2.has_children()) {
                for (auto child2 : node2.children()) {
                    ignoredEditorIDs.emplace_back(yamlNodeToString(child2));
                }
            }
            std::list<TrackedArmorPair> outfits;
            const auto node3 = child["outfits"];
            if (!node3.invalid() && node3.has_children()) {
                for (auto child3 : node3.children()) {
                    std::string formIdMilitary = yamlNodeToString(child3["military"]["formId"]);
                    std::string modNameMilitary = yamlNodeToString(child3["military"]["modName"]);
                    std::string provideMilitary = yamlNodeToString(child3["military"]["provide"]);
                    std::string formIdCivilian = yamlNodeToString(child3["civilian"]["formId"]);
                    std::string modNameCivilian = yamlNodeToString(child3["civilian"]["modName"]);
                    std::string provideCivilian = yamlNodeToString(child3["civilian"]["provide"]);
                    const std::uint32_t fidMilitary = parseHex(formIdMilitary);
                    const std::uint32_t fidCivilian = parseHex(formIdCivilian);
                    if (fidMilitary == 0) {
                        throw std::runtime_error(fmt::format("Invalid formId: {}", formIdMilitary));
                    }
                    if (fidCivilian == 0) {
                        throw std::runtime_error(fmt::format("Invalid formId: {}", formIdCivilian));
                    }
                    const auto formMilitary = RE::TESDataHandler::GetSingleton()->LookupForm(fidMilitary, modNameMilitary);
                    if (!formMilitary) {
                        logger::warn("Can't find military form {} from {}", formIdMilitary, modNameMilitary);
                        continue;
                    }
                    const auto formCivilian = RE::TESDataHandler::GetSingleton()->LookupForm(fidCivilian, modNameCivilian);
                    if (!formCivilian) {
                        logger::warn("Can't find civilian form {} from {}", formIdCivilian, modNameCivilian);
                        continue;
                    }
                    auto armorMilitary = formMilitary->As<RE::TESObjectARMO>();
                    if (!armorMilitary) {
                        logger::error("Military form {} from {} is not armor", formIdMilitary, modNameMilitary);
                        continue;
                    }
                    auto armorCivilian = formCivilian->As<RE::TESObjectARMO>();
                    if (!armorCivilian) {
                        logger::error("Civilian form {} from {} is not armor", formIdCivilian, modNameCivilian);
                        continue;
                    }
                    outfits.emplace_back(armorMilitary, armorCivilian, provideCivilian!="false", provideMilitary!="false");
                }
            }
            if (npc->GetActorBase() && npc->GetActorBase()->IsUnique()) {
                npcs.try_emplace(npc->GetFormID(), npc, ignoredEditorIDs, civilianKeywords, outfits, magicEffect, factions);
            }
        }
        void handleFile(const std::string& p, RE::Effect* magicEffect, const std::list<RE::TESFaction*>& factions) {
            if (!p.ends_with(".yml") && !p.ends_with(".yaml")) {
                return;
            }
            std::ifstream inputFile(p, std::ios::in);
            if (!inputFile.good()) {
                throw std::runtime_error(fmt::format("Could not open file: {}", p));
            }
            std::string data;
            for (std::string line; std::getline(inputFile, line); ) {
                data.append(line);
                data.append("\n");
            }
            inputFile.close();

            c4::substr sus = c4::to_substr(data);
            c4::yml::Tree tree = ryml::parse_in_place(sus);
            const c4::yml::NodeRef node = tree["npcs"];
            if (node.invalid()) {
                throw std::runtime_error("Could not get npc list");
            }
            const c4::yml::NodeRef node2 = tree["civilianLocations"];
            std::list<std::string> civilianKeywords;
            if (!node2.invalid() && node2.has_children()) {
                for (auto child : node2.children()) {
                    if (!child.invalid() && !child.val_is_null()) {
                        civilianKeywords.emplace_back(yamlNodeToString(child));
                    }
                }
            }
            if (node.has_children()) {
                logger::trace("Going into npc list");
                for (auto child : node.children()) {
                    handleNPC(child, civilianKeywords, magicEffect, factions);
                }
            }
        }
        void setup()
        {
            logger::info("Loading configs from file system");
            auto currentBasePath = std::filesystem::current_path().string();
            int errors = 0;
            int amount = 0;
            {
                std::scoped_lock lock(npcsMutex);
                npcs.clear();
            }
            RE::Effect* magicEffect = nullptr;
            try {
                if (RE::TESForm* form = RE::TESDataHandler::GetSingleton()->LookupForm(parseHex("0x800"), "dz_undress_common.esp")) {
                    if (const auto effect = form->As<RE::Effect>()) {
                        magicEffect = effect;
                    }
                }
            } catch (std::exception& e) {
                logger::debug("Failed to retrieve undress effect for NPCs from dz_undress_common.esp - not an issue usually. {}", e.what());
            }
            std::list<RE::TESFaction*> factions;
            try {
                if (RE::TESForm* form = RE::TESDataHandler::GetSingleton()->LookupForm(parseHex("0x182E"), "OSA.esm")) {
                    if (const auto faction = form->As<RE::TESFaction>()) {
                        factions.emplace_back(faction);
                    }
                }
                if (RE::TESForm* form = RE::TESDataHandler::GetSingleton()->LookupForm(parseHex("0x182F"), "OSA.esm")) {
                    if (const auto faction = form->As<RE::TESFaction>()) {
                        factions.emplace_back(faction);
                    }
                }
                if (RE::TESForm* form = RE::TESDataHandler::GetSingleton()->LookupForm(parseHex("0x1830"), "OSA.esm")) {
                    if (const auto faction = form->As<RE::TESFaction>()) {
                        factions.emplace_back(faction);
                    }
                }
            } catch (std::exception& e) {
                logger::debug("Failed to retrieve undress factions from OSA.esm - not an issue usually. {}", e.what());
            }
            if (std::filesystem::exists("Data/skse/plugins/IdrinthOutfitSystem")) {
                logger::debug("Folder found, iterating");
                std::scoped_lock lock(npcsMutex);
                for (auto& file: std::filesystem::recursive_directory_iterator{"Data/skse/plugins/IdrinthOutfitSystem"}) {
                    if (file.is_regular_file()) {
                        amount++;
                        auto p = file.path().string();
                        logger::trace("Loading {}", p);
                        try {
                            handleFile(p, magicEffect, factions);
                        } catch (std::exception& e) {
                            logger::error("Failed to parse config {}: {}", p, e.what());
                            errors++;
                        }
                    }
                }
            }
            logger::info("Loaded {} configs from file system with {} errors", amount, errors);
        }
        RE::BSEventNotifyControl handle(RE::Actor* npc) const {
            if (!npc) {
                return RE::BSEventNotifyControl::kContinue;
            }
            const auto fId = npc->GetFormID();
            {
                std::scoped_lock lock(npcsMutex);
                if (!npcs.contains(fId)) {
                    return RE::BSEventNotifyControl::kContinue;
                }
            }
            auto* self = this;
            SKSE::GetTaskInterface()->AddTask([self, fId]() {
                std::scoped_lock lock(self->npcsMutex);
                const auto it = self->npcs.find(fId);
                if (it == self->npcs.end()) {
                    return;
                }
                const auto form = RE::TESForm::LookupByID(fId);
                if (!form) {
                    return;
                }
                const auto actor = form->As<RE::Actor>();
                if (!actor) {
                    return;
                }
                it->second.handleEquip(actor);
            });
            return RE::BSEventNotifyControl::kContinue;
        }
        RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* a_event, [[maybe_unused]]RE::BSTEventSource<RE::TESCombatEvent>* a_eventSource) override {
            if (!a_event || !a_event->actor) {
                return RE::BSEventNotifyControl::kContinue;
            }
            return handle(a_event->actor->As<RE::Actor>());
        }
        RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, [[maybe_unused]]RE::BSTEventSource<RE::TESEquipEvent>* a_eventSource) override {
            if (!a_event || !a_event->actor) {
                return RE::BSEventNotifyControl::kContinue;
            }
            return handle(a_event->actor->As<RE::Actor>());
        }
        RE::BSEventNotifyControl ProcessEvent(const RE::TESCellAttachDetachEvent* a_event, [[maybe_unused]]RE::BSTEventSource<RE::TESCellAttachDetachEvent>* a_eventSource) override {
            if (!a_event || !a_event->reference) {
                return RE::BSEventNotifyControl::kContinue;
            }
            return handle(a_event->reference->As<RE::Actor>());
        }
        RE::BSEventNotifyControl ProcessEvent(const RE::TESActorLocationChangeEvent* a_event, [[maybe_unused]]RE::BSTEventSource<RE::TESActorLocationChangeEvent>* a_eventSource) override {
            if (!a_event || !a_event->actor) {
                return RE::BSEventNotifyControl::kContinue;
            }
            return handle(a_event->actor->As<RE::Actor>());
        }
        RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* a_event, [[maybe_unused]]RE::BSTEventSource<RE::TESMagicEffectApplyEvent>* a_eventSource) override {
            if (!a_event || !a_event->target) {
                return RE::BSEventNotifyControl::kContinue;
            }
            return handle(a_event->target->As<RE::Actor>());
        }
        RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, [[maybe_unused]]RE::BSTEventSource<RE::TESObjectLoadedEvent>* a_eventSource) override {
            if (!a_event || !a_event->formID) {
                return RE::BSEventNotifyControl::kContinue;
            }
            const auto form = RE::TESForm::LookupByID(a_event->formID);
            if (!form) {
                return RE::BSEventNotifyControl::kContinue;
            }
            return handle(form->As<RE::Actor>());
        }

    private:
        std::map<RE::FormID, TrackedNPC> npcs;
        mutable std::recursive_mutex npcsMutex;
};

void OnMessage(SKSE::MessagingInterface::Message* message) {
    switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            logger::info("Setting up Event Handler.");
            EquipmentEventSink::getSingleton()->setup();
            logger::info("Setting up Event Handler finished.");
            return;
        default:
            return;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    const auto config = Config::getSingleton();
    SetupLogger(config.getLogLevel());

    logger::info("Setup with LogLevel {}", logLevelList[config.getLogLevel()]);

    auto* eventSink = EquipmentEventSink::getSingleton();

    auto* eventSourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
    eventSourceHolder->AddEventSink<RE::TESEquipEvent>(eventSink);
    eventSourceHolder->AddEventSink<RE::TESCombatEvent>(eventSink);
    eventSourceHolder->AddEventSink<RE::TESCellAttachDetachEvent>(eventSink);
    eventSourceHolder->AddEventSink<RE::TESActorLocationChangeEvent>(eventSink);
    eventSourceHolder->AddEventSink<RE::TESMagicEffectApplyEvent>(eventSink);
    eventSourceHolder->AddEventSink<RE::TESObjectLoadedEvent>(eventSink);

    auto* messagingInterface = SKSE::GetMessagingInterface();
    messagingInterface->RegisterListener(OnMessage);

    return true;
}