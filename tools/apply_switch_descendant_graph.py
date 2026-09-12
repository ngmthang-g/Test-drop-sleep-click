from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEST = ROOT / "tests/test_ui_direct_tab_contract.py"
BRIDGE = ROOT / "src/bridge_chunks/bridge_13.txt"


def patch_tests():
    s = TEST.read_text(encoding="utf-8")
    marker = "def test_switch_fallback_walks_descendant_graph_for_exact_runtime_nodes():"
    if marker in s:
        return
    block = r'''


def test_switch_fallback_walks_descendant_graph_for_exact_runtime_nodes():
    text = read("src/bridge_chunks/bridge_13.txt")
    for token in [
        "ScanSkillBarDescendantGraph",
        "ReadUiChildrenForSwitchScan",
        "FindSwitchTargetFromDescendantGraph",
        "ReadSkillBarSwitchStateFromDescendantGraph",
        "graphVisited=",
        "switchNodes=",
        "toggleFirstNodes=",
        "toggleSecondNodes=",
    ]:
        assert token in text
    # The exact switch/toggle nodes may exist only below a registered UIObject root.
    assert 'get_CoreChildren' in text and 'get_Children' in text
    assert 'buttonoriginalswitchsite' in text
    assert 'togglefirsttab' in text and 'togglesecondtab' in text
'''
    needle = "\ndef test_controller_has_separate_test_ui_tab_and_all_target_rows():"
    if needle not in s:
        raise SystemExit("test insertion anchor not found")
    TEST.write_text(s.replace(needle, block + needle, 1), encoding="utf-8")


def patch_bridge():
    s = BRIDGE.read_text(encoding="utf-8")
    marker = "bool ScanSkillBarDescendantGraph("
    if marker not in s:
        needle = "bool ReadSkillBarSwitchStateLive(SkillBarSwitchState& state,\n                                 wchar_t* detail, std::size_t cap) {"
        if needle not in s:
            raise SystemExit("bridge insertion anchor not found")
        insert = r'''bool ReadUiChildrenForSwitchScan(Il2CppObject* object, Il2CppClass* klass,
                                     std::vector<Il2CppObject*>& children) {
    children.clear();
    if (!object || !klass) return false;
    Il2CppObject* childrenArray = nullptr;
    if (!ObjectGetter(object, klass, "get_CoreChildren", childrenArray) || !childrenArray)
        (void)ObjectGetter(object, klass, "get_Children", childrenArray);
    if (!childrenArray) return true;
    return ReadManagedPointerArray(childrenArray, children, 256);
}

struct SkillBarDescendantGraph {
    std::size_t visited = 0;
    std::vector<UiControl> switchNodes;
    std::vector<UiControl> toggleFirstNodes;
    std::vector<UiControl> toggleSecondNodes;
    std::vector<UiControl> hints;
};

void AddUniqueUiNode(std::vector<UiControl>& list, UiControl&& value) {
    for (const UiControl& existing : list)
        if (existing.object == value.object) return;
    list.push_back(std::move(value));
}

bool BuildLooseUiNode(Il2CppObject* object, Il2CppClass* klass, UiControl& row) {
    if (!object || !klass) return false;
    row = {};
    row.object = object;
    row.klass = klass;
    UiKind kind{};
    row.kind = ClassifyControl(klass, kind) ? kind : UiKind::Rect;
    (void)ReadUiString(object, klass, "Name", row.labels.name);
    (void)ReadUiString(object, klass, "Text", row.labels.text);
    (void)ReadUiString(object, klass, "Tag", row.tag);
    if (!ReadUiString(object, klass, "ClickHandler", row.labels.handler))
        (void)ReadUiString(object, klass, "PointerClickHandler", row.labels.handler);
    return true;
}

bool ScanSkillBarDescendantGraph(const std::vector<UiControl>& active,
                                 SkillBarDescendantGraph& graph,
                                 wchar_t* detail, std::size_t cap) {
    graph = {};
    std::vector<Il2CppObject*> pending;
    pending.reserve(active.size() + 256);
    for (const UiControl& row : active) if (row.object) pending.push_back(row.object);
    std::vector<Il2CppObject*> seen;
    seen.reserve(1024);

    while (!pending.empty() && seen.size() < 8192) {
        Il2CppObject* current = pending.back();
        pending.pop_back();
        if (!current || std::find(seen.begin(), seen.end(), current) != seen.end()) continue;
        seen.push_back(current);
        graph.visited = seen.size();

        Il2CppClass* klass = nullptr;
        if (!ReadLocal(current, 0, klass) || !klass) continue;
        const MethodInfo* activeGetter = FindMethod(klass, "get_ActiveInHierarchy", 0);
        if (activeGetter) {
            std::int32_t isActive = 0;
            wchar_t ignored[128]{};
            if (ScalarGetter(klass, "get_ActiveInHierarchy", current, isActive, ignored, _countof(ignored)) && !isActive)
                continue;
        }

        UiControl row{};
        if (!BuildLooseUiNode(current, klass, row)) continue;
        const std::wstring name = FoldKey(row.labels.name);
        const std::wstring handler = FoldKey(row.labels.handler);
        const bool isSwitch = name == L"buttonoriginalswitchsite" ||
                              handler == L"buttonoriginalswitchsiteclicked";
        const bool isFirst = name == L"togglefirsttab";
        const bool isSecond = name == L"togglesecondtab";
        const bool isHint = isSwitch || isFirst || isSecond ||
            HasAny(name + handler, {L"skillbar", L"switchsite", L"buttonoriginal", L"togglefirst", L"togglesecond"});

        if (isSwitch || isFirst || isSecond || (isHint && graph.hints.size() < 3))
            (void)ReadAncestors(row);
        if (isSwitch) AddUniqueUiNode(graph.switchNodes, UiControl(row));
        if (isFirst) AddUniqueUiNode(graph.toggleFirstNodes, UiControl(row));
        if (isSecond) AddUniqueUiNode(graph.toggleSecondNodes, UiControl(row));
        if (isHint && graph.hints.size() < 3) AddUniqueUiNode(graph.hints, std::move(row));

        std::vector<Il2CppObject*> children;
        if (!ReadUiChildrenForSwitchScan(current, klass, children)) continue;
        for (Il2CppObject* child : children) if (child) pending.push_back(child);
    }

    if (seen.size() >= 8192 && !pending.empty()) {
        SetText(detail, cap, L"SWITCH GRAPH LIMIT • quá 8192 node; không callback");
        return false;
    }
    return true;
}

void AppendSkillBarGraphDiagnostics(const SkillBarDescendantGraph& graph,
                                    wchar_t* detail, std::size_t cap) {
    Append(detail, cap, L" • graphVisited="); AppendInt(detail, cap, static_cast<int>(graph.visited));
    Append(detail, cap, L" • switchNodes="); AppendInt(detail, cap, static_cast<int>(graph.switchNodes.size()));
    Append(detail, cap, L" • toggleFirstNodes="); AppendInt(detail, cap, static_cast<int>(graph.toggleFirstNodes.size()));
    Append(detail, cap, L" • toggleSecondNodes="); AppendInt(detail, cap, static_cast<int>(graph.toggleSecondNodes.size()));
    int emitted = 0;
    for (const UiControl& hint : graph.hints) {
        if (emitted++ >= 2) break;
        Append(detail, cap, L" • hint[Name="); Append(detail, cap, hint.labels.name.c_str());
        Append(detail, cap, L" Handler="); Append(detail, cap, hint.labels.handler.c_str());
        Append(detail, cap, L" Parent="); Append(detail, cap, hint.labels.ancestors.c_str());
        Append(detail, cap, L"]");
    }
}

bool ReadSkillBarSwitchStateFromDescendantGraph(SkillBarSwitchState& state,
                                                wchar_t* detail, std::size_t cap) {
    state = SkillBarSwitchState::Unknown;
    std::vector<UiControl> active;
    if (!EnumerateActiveUiObjects(active, detail, cap)) return false;
    SkillBarDescendantGraph graph{};
    if (!ScanSkillBarDescendantGraph(active, graph, detail, cap)) return false;

    std::vector<UiControl> first;
    std::vector<UiControl> second;
    for (UiControl& row : graph.toggleFirstNodes) {
        UiControl toggle{};
        if (PromoteActiveUiToToggle(row, toggle)) AddUniqueUiNode(first, std::move(toggle));
    }
    for (UiControl& row : graph.toggleSecondNodes) {
        UiControl toggle{};
        if (PromoteActiveUiToToggle(row, toggle)) AddUniqueUiNode(second, std::move(toggle));
    }

    if (first.size() != 1 || second.size() != 1) {
        SetText(detail, cap, L"SWITCH STATE UNKNOWN • descendant graph cần đúng 1 ToggleFirstTab + 1 ToggleSecondTab; không callback");
        AppendSkillBarGraphDiagnostics(graph, detail, cap);
        return false;
    }

    std::int32_t firstSelected = 0, secondSelected = 0;
    wchar_t ignored[128]{};
    if (!ScalarGetter(first[0].klass, "get_Selected", first[0].object, firstSelected, ignored, _countof(ignored)) ||
        !ScalarGetter(second[0].klass, "get_Selected", second[0].object, secondSelected, ignored, _countof(ignored))) {
        SetText(detail, cap, L"SWITCH STATE UNKNOWN • descendant toggle không đọc được get_Selected; không callback");
        AppendSkillBarGraphDiagnostics(graph, detail, cap);
        return false;
    }
    if ((firstSelected != 0) == (secondSelected != 0)) {
        SetText(detail, cap, L"SWITCH STATE UNKNOWN • descendant toggle có Selected không hợp lệ; không callback");
        AppendSkillBarGraphDiagnostics(graph, detail, cap);
        return false;
    }
    state = firstSelected ? SkillBarSwitchState::BagUi : SkillBarSwitchState::Skills;
    return true;
}

bool FindSwitchTargetFromDescendantGraph(UiTestTarget target,
                                         const std::vector<UiControl>& active,
                                         std::vector<UiControl>& controls,
                                         std::size_t& selectedIndex,
                                         int& candidateCount,
                                         int& selectedScore,
                                         bool& found,
                                         wchar_t* detail, std::size_t cap) {
    SkillBarDescendantGraph graph{};
    if (!ScanSkillBarDescendantGraph(active, graph, detail, cap)) return false;

    struct Candidate { UiControl control{}; int score = 0; };
    std::vector<Candidate> candidates;
    for (UiControl& row : graph.switchNodes) {
        const int score = UiTestScore(target, row);
        if (score <= 0) continue;
        UiControl promoted{};
        if (!PromoteActiveUiToCallable(row, promoted)) continue;
        bool merged = false;
        for (Candidate& existing : candidates) {
            if (existing.control.object == promoted.object) {
                if (score > existing.score) existing.score = score;
                merged = true;
                break;
            }
        }
        if (!merged) candidates.push_back({std::move(promoted), score});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score > b.score;
        return reinterpret_cast<std::uintptr_t>(a.control.object) <
               reinterpret_cast<std::uintptr_t>(b.control.object);
    });
    candidateCount = static_cast<int>(candidates.size());
    if (candidates.empty()) {
        SetText(detail, cap, UiTestTargetName(target));
        Append(detail, cap, L" • NOT FOUND • descendant runtime graph");
        AppendSkillBarGraphDiagnostics(graph, detail, cap);
        return true;
    }
    if (candidates.size() > 1 && candidates[0].score == candidates[1].score &&
        candidates[0].control.object != candidates[1].control.object) {
        SetText(detail, cap, UiTestTargetName(target));
        Append(detail, cap, L" • AMBIGUOUS • descendant runtime graph • candidates=");
        AppendInt(detail, cap, candidateCount);
        AppendSkillBarGraphDiagnostics(graph, detail, cap);
        return false;
    }

    controls.clear();
    controls.reserve(candidates.size());
    for (Candidate& candidate : candidates) controls.push_back(std::move(candidate.control));
    selectedIndex = 0;
    selectedScore = candidates[0].score;
    found = true;
    return true;
}

'''
        s = s.replace(needle, insert + needle, 1)

    old = '''    if (first.size() != 1 || second.size() != 1) {\n        SetText(detail, cap, L"SWITCH STATE UNKNOWN • live scan cần đúng 1 ToggleFirstTab + 1 ToggleSecondTab; không callback");\n        AppendSwitchRuntimeDiagnostics(active, detail, cap);\n        return false;\n    }\n'''
    new = '''    if (first.size() != 1 || second.size() != 1) {\n        return ReadSkillBarSwitchStateFromDescendantGraph(state, detail, cap);\n    }\n'''
    if old in s:
        s = s.replace(old, new, 1)

    old2 = '''    if (!ScalarGetter(first[0].klass, "get_Selected", first[0].object, firstSelected, ignored, _countof(ignored)) ||\n        !ScalarGetter(second[0].klass, "get_Selected", second[0].object, secondSelected, ignored, _countof(ignored))) {\n        SetText(detail, cap, L"SWITCH STATE UNKNOWN • live toggle không đọc được get_Selected; không callback");\n        AppendSwitchRuntimeDiagnostics(active, detail, cap);\n        return false;\n    }\n    if ((firstSelected != 0) == (secondSelected != 0)) {\n        SetText(detail, cap, L"SWITCH STATE UNKNOWN • live toggle có Selected không hợp lệ; không callback");\n        AppendSwitchRuntimeDiagnostics(active, detail, cap);\n        return false;\n    }\n'''
    new2 = '''    if (!ScalarGetter(first[0].klass, "get_Selected", first[0].object, firstSelected, ignored, _countof(ignored)) ||\n        !ScalarGetter(second[0].klass, "get_Selected", second[0].object, secondSelected, ignored, _countof(ignored))) {\n        return ReadSkillBarSwitchStateFromDescendantGraph(state, detail, cap);\n    }\n    if ((firstSelected != 0) == (secondSelected != 0)) {\n        return ReadSkillBarSwitchStateFromDescendantGraph(state, detail, cap);\n    }\n'''
    if old2 in s:
        s = s.replace(old2, new2, 1)

    old3 = '''    candidateCount = static_cast<int>(candidates.size());\n    if (candidates.empty()) {\n        SetText(detail, cap, UiTestTargetName(target));\n        Append(detail, cap, L" • NOT FOUND • wide runtime fallback");\n        AppendSwitchRuntimeDiagnostics(active, detail, cap);\n        return true;\n    }\n'''
    new3 = '''    candidateCount = static_cast<int>(candidates.size());\n    if (candidates.empty()) {\n        return FindSwitchTargetFromDescendantGraph(target, active, controls, selectedIndex,\n                                                   candidateCount, selectedScore, found, detail, cap);\n    }\n'''
    if old3 in s:
        s = s.replace(old3, new3, 1)

    BRIDGE.write_text(s, encoding="utf-8")


patch_tests()
patch_bridge()
