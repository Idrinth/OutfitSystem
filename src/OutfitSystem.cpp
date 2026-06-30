#include "PCH.h"
#include "logger.h"
#include "ryml/ryml.hpp"
#include "c4/substr.hpp"
#include "c4/std/string.hpp"
#include <charconv>
#include <utility>
#include <map>
#include <set>
#include <unordered_set>

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
std::string str2lower(std::string s)
{
    std::ranges::transform(
        s,
        s.begin(),
        [](const unsigned char c){ return std::tolower(c); }
    );
    return s;
}

class Config {
    public:
        [[nodiscard]] spdlog::level::level_enum getLogLevel() const {
            return logLevel;
        }
        [[nodiscard]] bool getLoadEager() const {
            return loadEager;
        }
        static Config getSingleton() noexcept {
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
                if (const c4::yml::NodeRef node = tree["logLevel"]; !node.invalid() && !node.val_is_null()) {
                    if (const std::string out = yamlNodeToString(node); logLevelMap.contains(out)) {
                        instance.logLevel = logLevelMap[out];
                    }
                }
                if (const c4::yml::NodeRef node = tree["loadEager"]; !node.invalid() && !node.val_is_null()) {
                    instance.loadEager = yamlNodeToString(node) == "true";
                }
            }

            return instance;
        }
    private:
        spdlog::level::level_enum logLevel = spdlog::level::level_enum::err;
        bool loadEager = false;
};
class LocationKeywordCache {
    public:
        LocationKeywordCache() = default;
        LocationKeywordCache(const LocationKeywordCache&) = delete;
        LocationKeywordCache(LocationKeywordCache&&) = delete;
        LocationKeywordCache& operator=(const LocationKeywordCache&) = delete;
        LocationKeywordCache& operator=(const LocationKeywordCache&&) = delete;
        bool hasKeyword(const std::string& keyword, RE::BGSLocation* location) {
            std::scoped_lock lock(locationMapMutex);
            return hasKeywordWrapper(keyword, location);
        }
        static LocationKeywordCache* getSingleton()
        {
            static LocationKeywordCache instance;
            return &instance;
        }
        void clear() {
            if (Config::getSingleton().getLoadEager()) {
                logger::info("Skipping clear, load eager is configured.");
                return;
            }
            std::scoped_lock lock(locationMapMutex);
            locationMap.clear();
            logger::info("Cleared cached locations.");
        }
        void primeCache(const std::list<std::string>& keywords) {
            if (!Config::getSingleton().getLoadEager()) {
                logger::info("Skipping priming, load eager is not configured.");
                return;
            }
            logger::info("Starting priming the location cache.");
            std::scoped_lock lock(locationMapMutex);
            auto* handler = RE::TESDataHandler::GetSingleton();
            if (!handler) {
                return;
            }
            for (auto* loc : handler->GetFormArray<RE::BGSLocation>()) {
                for (const auto& kw : keywords) {
                    if (hasKeywordWrapper(kw, loc)) {
                        logger::debug("Location {} has keyword {}", loc->GetName(), kw);
                    } else {
                        logger::debug("Location {} does not have keyword {}", loc->GetName(), kw);
                    }
                }
            }
            logger::info("Finished priming the location cache.");
        }
    private:
        bool hasKeywordWrapper(const std::string& keyword, RE::BGSLocation* location) {
            if (!location) {
                logger::trace("Found no keyword {} for empty location", keyword);
                return false;
            }
            auto& locationInMap = locationMap[location];
            if (const auto keywordInMap = locationInMap.find(keyword); keywordInMap != locationInMap.end()) {
                logger::trace("Found keyword {} for location {} from cache", keyword, location->GetName());
                return keywordInMap->second;
            }
            const auto result = hasKeywordInternal(keyword, location);
            locationInMap[keyword] = result;
            if (result) {
                logger::trace("Found keyword {} for location {} by iterating", keyword, location->GetName());
                return true;
            }
            logger::trace("Didn't find keyword {} for location {} by iterating", keyword, location->GetName());
            return result;
        }
        bool hasKeywordInternal(const std::string& keyword, const RE::BGSLocation* location) {
            if (location->HasKeywordString(keyword)) {
                logger::trace("Found keyword {} by manually searching for it with HasKeyword", keyword);
                return true;
            }
            for (const auto& kw : location->GetKeywords()) {
                if (kw->GetFormEditorID() && str2lower(kw->GetFormEditorID()) == str2lower(keyword)) {
                    logger::trace("Found keyword {} by manually searching for it case insensitive", keyword);
                    return true;
                }
            }
            return hasKeywordWrapper(keyword, location->parentLoc);
        }
        mutable std::mutex locationMapMutex;
        std::unordered_map<RE::BGSLocation*, std::unordered_map<std::string, bool>> locationMap;
};
class InitializedNPCsCache {
    public:
        InitializedNPCsCache() = default;
        InitializedNPCsCache(const InitializedNPCsCache&) = delete;
        InitializedNPCsCache(InitializedNPCsCache&&) = delete;
        InitializedNPCsCache& operator=(const InitializedNPCsCache&) = delete;
        InitializedNPCsCache& operator=(const InitializedNPCsCache&&) = delete;
        bool mayInitialize(RE::FormID formID) {
            std::scoped_lock lock(setMutex);
            if (formIDs.contains(formID)) {
                return false;
            }
            formIDs.emplace(formID);
            return true;
        }
        static InitializedNPCsCache* getSingleton()
        {
            static InitializedNPCsCache instance;
            return &instance;
        }
        void clear() {
            std::scoped_lock lock(setMutex);
            formIDs.clear();
        }
        std::unordered_set<RE::FormID> getAll() {
            return formIDs;
        }
    private:
        std::unordered_set<RE::FormID> formIDs;
        std::mutex setMutex;
};
class TrackedArmorPair {
    public:
        TrackedArmorPair(RE::TESObjectARMO* a_military, RE::TESObjectARMO* a_civilian, bool a_provideMilitary = true, bool a_provideCivilian = true)
            : military(a_military), civilian(a_civilian), provideMilitary(a_provideMilitary), provideCivilian(a_provideCivilian) {}
        RE::TESObjectARMO* military;
        RE::TESObjectARMO* civilian;
        bool provideMilitary;
        bool provideCivilian;
};
class UniqueFormIDQueue {
    public:
        UniqueFormIDQueue() = default;
        RE::FormID pop() {
            std::scoped_lock lock(queueMutex);
            const auto out = queue.front();
            queue.pop();
            formIDs.erase(out);
            return out;
        }
        void push(RE::FormID formID) {
            std::scoped_lock lock(queueMutex);
            if (formIDs.find(formID) != formIDs.end()) {
                logger::trace("FormID {} already exists", formID);
                return;
            }
            queue.emplace(formID);
            formIDs.try_emplace(formID, formID);
        }
        bool empty() {
            std::scoped_lock lock(queueMutex);
            return queue.empty();
        }
    private:
        std::unordered_map<RE::FormID, RE::FormID> formIDs;
        std::queue<RE::FormID> queue;
        std::mutex queueMutex;
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
        bool handleEquip(RE::Actor* npc) const {
            if (processing.exchange(true)) {
                return true;
            }
            struct Guard { std::atomic_bool& f; ~Guard(){ f = false; } } guard{processing};

            if (!npc) {
                return true;
            }
            logger::debug("Equipping NPC {}", npc->GetName());

            if (npc->IsDead() || npc->IsDisabled()) {
                logger::debug("NPC {} is disabled or dead", npc->GetName());
                return true;
            }
            if (!npc->Is3DLoaded()) {
                logger::debug("NPC {} is not loaded)", npc->GetName());
                return false;
            }
            if (!npc->HasContainer()) {
                logger::debug("NPC {} has no inventory", npc->GetName());
                return true;
            }

            auto* invChanges = npc->GetInventoryChanges();
            if (!invChanges || !invChanges->entryList) {
                logger::debug("NPC {} inventory not ready yet", npc->GetName());
                return false;
            }

            const auto counts = npc->GetInventoryCounts();
            std::set<RE::TESBoundObject*> worn;
            for (const auto* entry : *invChanges->entryList) {
                if (!entry || !entry->object || !entry->extraLists) {
                    continue;
                }
                for (const auto* xList : *entry->extraLists) {
                    if (!xList) {
                        continue;
                    }
                    if (xList->HasType(RE::ExtraDataType::kWorn) || xList->HasType(RE::ExtraDataType::kWornLeft)) {
                        worn.insert(entry->object);
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

            if (InitializedNPCsCache::getSingleton()->mayInitialize(npc->GetFormID())) {
                logger::debug("NPC {} is uninitialized, handling now", npc->GetName());
                npc->SetDefaultOutfit(nullptr, false);
                npc->SetSleepOutfit(nullptr, false);
                for (const auto& armorPair : gear) {
                    const auto military = armorPair.military;
                    const auto civilian = armorPair.civilian;
                    if (!military || !civilian) {
                        continue;
                    }
                    if (armorPair.provideMilitary && military->GetFormEditorID() && !isInList(military->GetFormEditorID(), ignoredArmorEditorIDs) && !hasItem(military)) {
                        npc->AddObjectToContainer(military, nullptr, 1, nullptr);
                    }
                    if (armorPair.provideCivilian && military != civilian && civilian->GetFormEditorID() && !isInList(civilian->GetFormEditorID(), ignoredArmorEditorIDs) && !hasItem(civilian)) {
                        npc->AddObjectToContainer(civilian, nullptr, 1, nullptr);
                    }
                }
                logger::debug("NPC {} initialised, deferring equip to next tick", npc->GetName());
                return false;
            }
            if (magicEffectBathUndress && !npc->IsInCombat() && npc->AsMagicTarget() && npc->AsMagicTarget()->HasMagicEffect(magicEffectBathUndress->baseEffect)) {
                handleUnequip(npc, worn);
                logger::debug("Unequipping NPC {} due to magic effect done", npc->GetName());
                return true;
            }
            for (const auto& faction : factionsOSA) {
                if (npc->IsInFaction(faction)) {
                    handleUnequip(npc, worn);
                    logger::debug("Unequipping NPC {} due to factions done", npc->GetName());
                    return true;
                }
            }
            const bool isCivilian = !npc->IsInCombat() && isInCivilianLocation(npc->GetCurrentLocation());
            logger::debug("NPC is in civilian location? {}", isCivilian);
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
            return true;
        }
    private:
        bool isInCivilianLocation(RE::BGSLocation* location) const {
            if (!location) {
                logger::trace("Location is nullptr");
                return false;
            }
            if (LocationKeywordCache::getSingleton()->hasKeyword("LocTypeClearable", location) && !location->IsCleared()) {
                logger::trace("Location is dangerous");
                return false;
            }
            for (const auto& keyword : civilianKeywords) {
                if (LocationKeywordCache::getSingleton()->hasKeyword(keyword, location)) {
                    logger::trace("Location has keyword {}", keyword);
                    return true;
                }
            }
            logger::trace("Location is not civilian");
            return false;
        }
        std::list<TrackedArmorPair> gear;
        std::list<std::string> civilianKeywords;
        RE::Effect* magicEffectBathUndress;
        std::list<RE::TESFaction*> factionsOSA;
        std::list<std::string> ignoredArmorEditorIDs;
        mutable std::atomic_bool processing{false};
};
class NPCRetryEntry {
    public:
        NPCRetryEntry(const RE::FormID formId) {
            fId = formId;
        }
        [[nodiscard]] RE::FormID getFormId() const {
            return fId;
        }
        void incrementRetry() {
            std::scoped_lock lock(processing);
            retryCount++;
        }
        void resetRetry() {
            std::scoped_lock lock(processing);
            retryCount = 0;
        }
        [[nodiscard]] bool mayRetry() const {
            return retryCount < 5;
        }
    private:
        RE::FormID fId;
        int retryCount = 0;
        mutable std::mutex processing;
};
class EquipmentEventHandler {
    public:
        void setup(RE::Effect* magicEffect) {
            logger::info("Loading configs from file system");
            auto currentBasePath = std::filesystem::current_path().string();
            std::list<std::string> configuredKeywords;
            configuredKeywords.emplace_back("LocTypeClearable");
            int errors = 0;
            int amount = 0;
            {
                std::scoped_lock lock(npcsMutex);
                npcs.clear();
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
                            for (const auto& keyword : handleFile(p, factions, magicEffect)) {
                                configuredKeywords.emplace_back(keyword);
                            }
                        } catch (std::exception& e) {
                            logger::error("Failed to parse config {}: {}", p, e.what());
                            errors++;
                        }
                    }
                }
            }
            logger::info("Loaded {} configs from file system with {} errors", amount, errors);
            if (Config::getSingleton().getLoadEager()) {
                LocationKeywordCache::getSingleton()->primeCache(configuredKeywords);
            }
        }
        void queue(const RE::Actor* npc) const {
            internalHandler(npc);
        }
        void queuedHandler() {
            logger::trace("Queued Handler firing");
            if (handleQueue()) {
                auto self = this;
                SKSE::GetTaskInterface()->AddTask([self]() {
                    self->queuedHandler();
                });
            } else {
                scheduleRetry(150);
            }
        }
        void start() {
            static std::atomic_bool started{false};
            if (started.exchange(true)) {
                return;
            }
            scheduleRetry(150);
        }
        void enable(const RE::Actor* actor, const bool enable) const {
            if (!actor || !actor->GetFormID()) {
                return;
            }
            const auto fId = actor->GetFormID();
            std::scoped_lock lock(npcsMutex);
            if (!npcs.contains(fId)) {
                logger::debug("En-/Disabling Actor {} can't be done because it is unhandled.", actor->GetFormID());
                return;
            }
            if (enable && paused.contains(fId)) {
                paused.erase(fId);
                queue(actor);
            }
            if (!enable && !paused.contains(fId)) {
                paused.insert(fId);
            }
        }
        bool enabled(const RE::Actor* actor) const {
            if (!actor || !actor->GetFormID()) {
                return true;
            }
            std::scoped_lock lock(todosMutex);
            return !paused.contains(actor->GetFormID());
        }
    private:
        bool handleQueue() {
            RE::FormID fId;
            {
                std::scoped_lock lock(todosMutex);
                if (todos.empty()) {
                    bool hadContent = false;
                    std::scoped_lock lock2(nextTodosMutex);
                    while (!nextTodos.empty()) {
                        todos.push(nextTodos.pop());
                        hadContent = true;
                    }
                    return hadContent;
                }
                fId = todos.pop();
                if (!paused.contains(fId)) {
                    std::scoped_lock lock2(nextTodosMutex);
                    nextTodos.push(fId);
                    return false;
                }
            }
            queuedHandle(fId);
            return true;
        }
        void scheduleRetry(int delay) {
            scheduleRetry(std::chrono::milliseconds(delay));
        }
        void scheduleRetry(std::chrono::milliseconds delay) {
            auto self = this;
            std::thread([delay, self]() {
                std::this_thread::sleep_for(delay);
                SKSE::GetTaskInterface()->AddTask([self]() {
                    self->handleQueue();
                });
            }).detach();
        }
        void internalHandler(const RE::Actor* npc) const {
            if (!npc) {
                return;
            }
            const auto fId = npc->GetFormID();
            {
                std::scoped_lock lock(npcsMutex);
                if (!npcs.contains(fId)) {
                    return;
                }
            }
            std::scoped_lock lock(nextTodosMutex);
            std::scoped_lock lock2(npcsMutex);
            if (const auto found = npcRetries.find(fId); found != npcRetries.end()) {
                found->second.resetRetry();
                nextTodos.push(fId);
            }
        }
        void handleNPC(const c4::yml::ConstNodeRef child, const std::list<std::string>& civilianKeywords, const std::list<RE::TESFaction*>& factions, RE::Effect* magicEffect) {
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
                    outfits.emplace_back(armorMilitary, armorCivilian, provideMilitary!="false", provideCivilian!="false");
                }
            }
            if (npc->GetActorBase() && npc->GetActorBase()->IsUnique()) {
                npcs.try_emplace(npc->GetFormID(), npc, ignoredEditorIDs, civilianKeywords, outfits, magicEffect, factions);
                npcRetries.try_emplace(npc->GetFormID(), npc->GetFormID());
            }
        }
        std::list<std::string> handleFile(const std::string& p, const std::list<RE::TESFaction*>& factions, RE::Effect* magicEffect) {
            if (!p.ends_with(".yml") && !p.ends_with(".yaml")) {
                return {};
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
                        auto keyword = yamlNodeToString(child);
                        civilianKeywords.emplace_back(keyword);
                    }
                }
            }
            if (node.has_children()) {
                logger::trace("Going into npc list");
                for (auto child : node.children()) {
                    handleNPC(child, civilianKeywords, factions, magicEffect);
                }
            }
            return civilianKeywords;
        }
        void queuedHandle(const RE::FormID fId) {
            if (!fId) {
                return;
            }
            const auto npcRetryEntryOut = npcRetries.find(fId);
            if (npcRetryEntryOut == npcRetries.end()) {
                return;
            }
            const auto npcRetryEntry = &npcRetryEntryOut->second;
            if (!npcRetryEntry || !npcRetryEntry->mayRetry() || !npcRetryEntry->getFormId()) {
                return;
            }
            npcRetryEntry->incrementRetry();
            bool needsRetry = false;
            {
                std::scoped_lock lock(npcsMutex);
                const auto it = npcs.find(npcRetryEntry->getFormId());
                if (it == npcs.end()) {
                    return;
                }
                const auto form = RE::TESForm::LookupByID(npcRetryEntry->getFormId());
                if (!form) {
                    return;
                }
                const auto actor = form->As<RE::Actor>();
                if (!actor) {
                    return;
                }
                needsRetry = !it->second.handleEquip(actor);
            }
            if (needsRetry) {
                std::scoped_lock lock2(nextTodosMutex);
                nextTodos.push(fId);
            }
        }
        std::map<RE::FormID, TrackedNPC> npcs;
        mutable std::map<RE::FormID, NPCRetryEntry> npcRetries;
        mutable std::recursive_mutex npcsMutex;
        mutable std::recursive_mutex todosMutex;
        mutable std::recursive_mutex nextTodosMutex;
        mutable UniqueFormIDQueue todos;
        mutable UniqueFormIDQueue nextTodos;
        mutable std::set<RE::FormID> paused;
};
class EquipmentEventSink: public RE::BSTEventSink<RE::TESContainerChangedEvent>, public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>, public RE::BSTEventSink<RE::TESActiveEffectApplyRemoveEvent>, public RE::BSTEventSink<RE::TESCombatEvent>, public RE::BSTEventSink<RE::TESEquipEvent>, public RE::BSTEventSink<RE::TESCellAttachDetachEvent>, public RE::BSTEventSink<RE::TESActorLocationChangeEvent>, public RE::BSTEventSink<RE::TESObjectLoadedEvent> {
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
        void enable(RE::Actor* actor, const bool enable) {
            handler.enable(actor, enable);
        }
        bool enabled(RE::Actor* actor) {
            return handler.enabled(actor);
        }
        void setup()
        {
            magicEffect = nullptr;
            try {
                if (RE::TESForm* form = RE::TESDataHandler::GetSingleton()->LookupForm(parseHex("0x800"), "dz_undress_common.esp")) {
                    if (const auto effect = form->As<RE::Effect>()) {
                        magicEffect = effect;
                    }
                }
            } catch (std::exception& e) {
                logger::debug("Failed to retrieve undress effect for NPCs from dz_undress_common.esp - not an issue usually. {}", e.what());
            }
            handler.setup(magicEffect);
        }
        RE::BSEventNotifyControl handle(const RE::Actor* npc) const {
            handler.queue(npc);
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
            if (!a_event || !a_event->target || !magicEffect || !magicEffect->baseEffect) {
                return RE::BSEventNotifyControl::kContinue;
            }
            if (a_event->magicEffect != magicEffect->baseEffect->formID) {
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
        RE::BSEventNotifyControl ProcessEvent(const RE::TESActiveEffectApplyRemoveEvent* a_event, [[maybe_unused]]RE::BSTEventSource<RE::TESActiveEffectApplyRemoveEvent>* a_eventSource) override {
            if (!a_event || !a_event->target) {
                return RE::BSEventNotifyControl::kContinue;
            }
            if (a_event->isApplied == true) {
                return RE::BSEventNotifyControl::kContinue;
            }
            return handle(a_event->target->As<RE::Actor>());
        }
        RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, [[maybe_unused]]RE::BSTEventSource<RE::TESContainerChangedEvent>* a_eventSource) override {
            if (!a_event) {
                return RE::BSEventNotifyControl::kContinue;
            }
            const auto target = RE::TESForm::LookupByID(a_event->newContainer);
            const auto source = RE::TESForm::LookupByID(a_event->oldContainer);
            if (target) {
                handle(target->As<RE::Actor>());
            }
            if (source) {
                handle(source->As<RE::Actor>());
            }
            return RE::BSEventNotifyControl::kContinue;
        }
        void scheduleHandler() {
            handler.start();
        }
    private:
        RE::Effect* magicEffect = nullptr;
        EquipmentEventHandler handler;
};

void OnMessage(SKSE::MessagingInterface::Message* message) {
    switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            logger::info("Setting up Event Handler.");
            EquipmentEventSink::getSingleton()->setup();
            logger::info("Setting up Event Handler finished.");
            return;
        case SKSE::MessagingInterface::kNewGame:
            logger::info("Setting up queue handler.");
            EquipmentEventSink::getSingleton()->scheduleHandler();
            logger::info("Queue handler setup.");
        case SKSE::MessagingInterface::kPreLoadGame:
            logger::info("Clearing location cache.");
            LocationKeywordCache::getSingleton()->clear();
            logger::info("Cleared location cache.");
            return;
        case SKSE::MessagingInterface::kPostLoadGame:
            logger::info("Setting up queue handler.");
            EquipmentEventSink::getSingleton()->scheduleHandler();
            logger::info("Queue handler setup.");
            return;
        default:
            return;
    }
}
inline const auto InitializedNPCRecord = _byteswap_ulong('IOSI');
void LoadCallback(SKSE::SerializationInterface* serializer)
{
    logger::debug("Clearing initialized NPCs after game load.");
    InitializedNPCsCache::getSingleton()->clear();
    std::uint32_t type;
    std::uint32_t size;
    std::uint32_t version;
    while (serializer->GetNextRecordInfo(type, version, size)) {
        if (type == InitializedNPCRecord) {
            std::size_t npcsSize;
            serializer->ReadRecordData(&npcsSize, sizeof(npcsSize));
            for (; npcsSize > 0; --npcsSize) {
                RE::FormID formId;
                serializer->ReadRecordData(&formId, sizeof(formId));
                RE::FormID currentFormId;
                serializer->ResolveFormID(formId, currentFormId);
                const auto actor = RE::Actor::LookupByID(currentFormId);
                if (!actor || !actor->As<RE::Actor>() || !InitializedNPCsCache::getSingleton()->mayInitialize(currentFormId)) {
                    logger::debug("Failed to set npc {} as initialized.", currentFormId);
                }
            }
        }
    }
}
void SaveCallback(SKSE::SerializationInterface* serializer) {
    logger::debug("Saving initialized NPCs.");
    if (!serializer->OpenRecord(InitializedNPCRecord, 1)) {
        logger::debug("Failed to open npc list.");
        return;
    }
    auto formIDs = InitializedNPCsCache::getSingleton()->getAll();
    std::size_t size = formIDs.size();
    if (!serializer->WriteRecordData(&size, sizeof(size))) {
        logger::debug("Failed to write NPC list header.");
        return;
    }
    for (auto formID : formIDs) {
        if (!serializer->WriteRecordData(&formID, sizeof(formID))) {
            logger::debug("Failed to write NPC list entry, continuing anyway.");
        }
    }
    logger::debug("Saved initialized NPCs.");
}
void disableSystem(RE::StaticFunctionTag*, RE::Actor* actor) {
    EquipmentEventSink::getSingleton()->enable(actor, false);
}
void enableSystem(RE::StaticFunctionTag*, RE::Actor* actor) {
    EquipmentEventSink::getSingleton()->enable(actor, true);
}
bool isSystemEnabled(RE::StaticFunctionTag*, RE::Actor* actor) {
    return EquipmentEventSink::getSingleton()->enabled(actor);
}
bool bindPapyrusFunctions(RE::BSScript::IVirtualMachine* vm) {
    vm->RegisterFunction("disable", "IdrinthOutfitSystem", disableSystem);
    vm->RegisterFunction("enable", "IdrinthOutfitSystem", enableSystem);
    vm->RegisterFunction("isEnabled", "IdrinthOutfitSystem", isSystemEnabled);
    return true;
}
SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    const auto &config = Config::getSingleton();
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
    eventSourceHolder->AddEventSink<RE::TESActiveEffectApplyRemoveEvent>(eventSink);

    auto* messagingInterface = SKSE::GetMessagingInterface();
    messagingInterface->RegisterListener(OnMessage);

    const auto serializer = SKSE::GetSerializationInterface();
    serializer->SetUniqueID(_byteswap_ulong('IOS_'));
    serializer->SetLoadCallback(LoadCallback);
    serializer->SetSaveCallback(SaveCallback);

    return true;
}