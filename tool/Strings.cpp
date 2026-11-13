#include "Strings.h"

#include <mkcontract/User.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <unordered_map>

namespace mkui::tool {
namespace {

struct Pair {
    const char* en;
    const char* ko;
};

// When new text is shown on screen, add its Korean here: mkui_strings_test fails on tr("...") text without a line.
// Korean follows engine docs/TOOL_DESIGN.md section 4: short, plain words, sentences in 해요체, buttons and menu items a verb noun
constexpr Pair kKorean[] = {
    {"No assets/ui folder in %s", "%s에 assets/ui 폴더가 없어요"},
    {"Cannot read %s", "%s 파일을 읽을 수 없어요"},
    {"Cannot write %s", "%s 파일을 쓸 수 없어요"},
    {"Saved %s", "%s 파일을 저장했어요"},
    {"Reloaded %s (changed on disk)", "%s 파일을 다시 불러왔어요 (디스크에서 바뀜)"},
    {"File", "파일"},
    {"Open project folder...", "프로젝트 폴더 열기..."},
    {"New screen...", "새 화면..."},
    {"Save", "저장"},
    {"Reload from disk", "디스크에서 다시 불러오기"},
    {"Quit", "끝내기"},
    {"Edit", "편집"},
    {"Undo", "실행 취소"},
    {"Redo", "다시 실행"},
    {"Interact", "조작"},
    {"On: the preview gets the mouse (buttons, sliders, inputs). Off: clicks select nodes", "켜면 미리보기가 마우스를 받아요 (버튼, 슬라이더, 입력). 끄면 클릭으로 노드를 골라요"},
    {"Language", "언어"},
    {"Cannot save the language to %s", "%s에 언어를 저장할 수 없어요"},
    {"Start", "시작"},
    {"MkUI Designer", "MkUI Designer"},
    {"Edits a project's assets/ui/*.ui. The game reads the same files.", "프로젝트의 assets/ui/*.ui를 편집해요. 게임도 같은 파일을 읽어요."},
    {"New screen", "새 화면"},
    {"Refresh", "새로 고침"},
    {"Copy the Penpot plugin address", "Penpot 플러그인 주소 복사"},
    {"Copied the Penpot plugin address: in Penpot, Plugins > Install, paste it", "Penpot 플러그인 주소를 복사했어요. Penpot의 Plugins > Install에 붙여 넣으세요"},
    {"Copies %s\nIn Penpot: Plugins > Install. Exports from the MealKit plugin are written into this project", "%s 주소를 복사해요\nPenpot의 Plugins > Install에 넣으면 MealKit 플러그인에서 내보낸 것이 이 프로젝트에 저장돼요"},
    {"Open a file", "파일을 여세요"},
    {"No board", "보드 없음"},
    {"Delete node", "노드 삭제"},
    {"Make component", "컴포넌트 만들기"},
    {"Moves this frame into ui/components/<name>.ui and leaves a \"use\" here", "이 프레임을 ui/components/<이름>.ui로 옮기고 여기에는 \"use\"를 남겨요"},
    {"Edit component", "컴포넌트 편집"},
    {"Penpot export: read-only", "Penpot에서 내보낸 것이라 읽기 전용이에요"},
    {"(default)", "(기본값)"},
    {"Layout", "레이아웃"},
    {"Placed by the parent's layout (%s)", "부모 레이아웃이 배치해요 (%s)"},
    {"Position", "위치"},
    {"Fixed", "고정"},
    {"Fit content", "내용에 맞춤"},
    {"Fill", "채우기"},
    {"A set number of units", "정한 크기"},
    {"As large as its children or text (hug)", "자식이나 글자만큼 (hug)"},
    {"Takes the room left in the parent's layout (fill)", "부모 레이아웃의 남은 공간을 채워요 (fill)"},
    {"Width", "너비"},
    {"Height", "높이"},
    {"Anchor", "기준"},
    {"Which edge it keeps its distance to when the screen is wider. leftright stretches, scale grows with the screen", "화면이 넓어질 때 거리를 유지할 가장자리예요. leftright는 늘어나고 scale은 화면과 함께 커져요"},
    {"Which edge it keeps its distance to when the screen is taller. topbottom stretches, scale grows with the screen", "화면이 길어질 때 거리를 유지할 가장자리예요. topbottom은 늘어나고 scale은 화면과 함께 커져요"},
    {"Drawn at %.0f, %.0f  size %.0f x %.0f", "그려진 위치 %.0f, %.0f  크기 %.0f x %.0f"},
    {"Children", "자식"},
    {"free (pos)", "자유 (pos)"},
    {"How the children are placed. Free: each by its own pos", "자식을 배치하는 방식이에요. 자유는 각자의 pos를 따라요"},
    {"Wrap", "줄바꿈"},
    {"Justify", "주축 정렬"},
    {"Align", "교차축 정렬"},
    {"Gap", "간격"},
    {"Padding", "안쪽 여백"},
    {"top, right, bottom, left", "위, 오른쪽, 아래, 왼쪽"},
    {"Padding  %s", "안쪽 여백  %s"},
    {"Style", "스타일"},
    {"Text color", "글자 색"},
    {"Add fill", "채우기 추가"},
    {"Corner radius", "모서리 둥글기"},
    {"Opacity", "불투명도"},
    {"Text", "글자"},
    {"{field} inserts a view model value", "{field}로 뷰 모델 값을 넣어요"},
    {"Translated text: key %s", "번역된 글자: 키 %s"},
    {"Font size", "글자 크기"},
    {"Font", "글꼴"},
    {"No .ttf or .otf in assets/fonts", "assets/fonts에 .ttf나 .otf가 없어요"},
    {"Text align", "글자 정렬"},
    {"Set text color", "글자 색 정하기"},
    {"Image", "이미지"},
    {"Image file", "이미지 파일"},
    {"(none)", "(없음)"},
    {"No pictures in assets/ui or assets/textures yet", "assets/ui나 assets/textures에 아직 그림이 없어요"},
    {"Copy a PNG into assets/ui and pick it here", "PNG를 assets/ui에 복사하고 여기서 고르세요"},
    {"Button (sends an action)", "버튼 (동작 보내기)"},
    {"Toggle (bool field)", "토글 (bool 필드)"},
    {"Slider (float field)", "슬라이더 (float 필드)"},
    {"List (repeats the first child)", "목록 (첫 자식 반복)"},
    {"Scroll area", "스크롤 영역"},
    {"Text input (string field)", "글자 입력 (string 필드)"},
    {"Behaviour", "동작"},
    {"Kind", "종류"},
    {"None (just drawn)", "없음 (그리기만)"},
    {"Game code reads the action from ctx.ui.actions()", "게임 코드가 ctx.ui.actions()에서 동작을 읽어요"},
    {"The field is a member of the screen's view model", "필드는 화면 뷰 모델의 멤버예요"},
    {"Select a node in the tree or the preview", "트리나 미리보기에서 노드를 고르세요"},
    {"Fix the source first", "먼저 소스를 고치세요"},
    {"All lines", "모든 줄"},
    {"Remove", "빼기"},
    {"add a line: fill #FF8800FF, on hover fill ...", "줄 추가: fill #FF8800FF, on hover fill ..."},
    {"Changed on disk.", "디스크에서 바뀌었어요."},
    {"Reload (drop my edits)", "다시 불러오기 (내 편집 버리기)"},
    {"action %s", "동작 %s"},
    {"Unsaved changes", "저장하지 않은 변경"},
    {"%s has unsaved changes.", "%s에 저장하지 않은 변경이 있어요."},
    {"Don't save", "저장 안 함"},
    {"Cancel", "취소"},
    {"Panel", "패널"},
    {"A box that lines its children up in a column (padding, gap, rounded corners)", "자식을 세로로 늘어놓는 상자예요 (안쪽 여백, 간격, 둥근 모서리)"},
    {"A line of text. {field} in it shows a view model value", "글자 한 줄이에요. 안의 {field}는 뷰 모델 값을 보여 줘요"},
    {"A picture from assets (set src to a PNG path)", "에셋의 그림이에요 (src에 PNG 경로를 넣으세요)"},
    {"Button", "버튼"},
    {"A button that sends an action when pressed", "누르면 동작을 보내는 버튼이에요"},
    {"Toggle", "토글"},
    {"An on/off switch for a bool field", "bool 필드를 켜고 끄는 스위치예요"},
    {"Slider", "슬라이더"},
    {"Drag to set a float field between a minimum and a maximum", "드래그해서 float 필드를 최솟값과 최댓값 사이로 정해요"},
    {"Progress bar", "진행 막대"},
    {"A bar filled by a float field from 0 to 1 (health, loading)", "float 필드의 0~1 값만큼 채워지는 막대예요 (체력, 로딩)"},
    {"Text input", "글자 입력"},
    {"A box the player types into (a string field)", "플레이어가 글자를 입력하는 상자예요 (string 필드)"},
    {"List", "목록"},
    {"Repeats its first child for every element of a list field", "목록 필드의 요소마다 첫 자식을 반복해요"},
    {"Clips its content and scrolls it with the wheel or the gamepad", "내용을 잘라 보여 주고 휠이나 게임패드로 스크롤해요"},
    {"%s exists already", "%s 파일이 이미 있어요"},
    {"Only a frame can become a component", "프레임만 컴포넌트로 만들 수 있어요"},
    {"Component %s (save the screen to keep the use)", "컴포넌트 %s (화면을 저장해야 use가 남아요)"},
    {"Save the open screen first", "먼저 열린 화면을 저장하세요"},
    {"Widgets", "위젯"},
    {"%s (click: add to the selection, drag: drop on the preview)", "%s (클릭: 선택한 곳에 추가, 드래그: 미리보기에 놓기)"},
    {"Components (ui/components)", "컴포넌트 (ui/components)"},
    {"Adds \"use %s %s\": the component's look, placed here", "\"use %s %s\"를 추가해요. 컴포넌트 모양이 여기에 놓여요"},
    {"  none yet: select a frame, then Make component", "  아직 없어요. 프레임을 고르고 컴포넌트 만들기를 누르세요"},
    {"Name", "이름"},
    {"New screen in assets/ui/ (letters, digits, _ and -)", "assets/ui/의 새 화면 (영문자, 숫자, _, -)"},
    {"Component file in assets/ui/components/", "assets/ui/components/의 컴포넌트 파일"},
    {"Create", "만들기"},
    {"Use these values", "이 값 쓰기"},
    {"Off: the screen as the game shows it with no view model bound", "끄면 뷰 모델 없이 게임이 보여 주는 화면 그대로예요"},
    {"This screen binds no fields", "이 화면은 연결한 필드가 없어요"},
    {"Saved in art/ui/samples/ (not shipped)", "art/ui/samples/에 저장돼요 (배포에는 빠져요)"},
    {"%d rows", "%d줄"},
    {"No answer", "응답 없음"},
    {"Bad answer", "잘못된 응답"},
    {"Took %d values of %s", "%s의 값 %d개를 가져왔어요"},
    {"Following (values update twice a second)", "따라가는 중 (값을 1초에 두 번 갱신해요)"},
    {"Follow stopped: the screen isn't open there", "따라가기를 멈췄어요. 그 화면이 열려 있지 않아요"},
    {"From a running game", "실행 중인 게임에서"},
    {"None running for this project. Start the game or press Play in the editor;", "이 프로젝트를 실행 중인 게임이 없어요. 게임을 켜거나 에디터에서 플레이를 누르세요."},
    {"saved .ui files reload there by themselves.", "저장한 .ui 파일은 거기서 저절로 다시 불러와요."},
    {"Take values", "값 가져오기"},
    {"Copies the view model this game has bound to %s into these values", "이 게임이 %s에 연결한 뷰 모델을 이 값으로 복사해요"},
    {"Follow", "따라가기"},
    {"  open: %s", "  열림: %s"},
    {"no screens", "화면 없음"},
    {"  Pick a .ui file on the left", "  왼쪽에서 .ui 파일을 고르세요"},
    {"%d%%   wheel zoom, middle drag pan, F fit, Shift snaps to 10", "%d%%   휠로 확대, 가운데 드래그로 이동, F로 맞추기, Shift로 10 단위 스냅"},
    {"Files", "파일"},
    {"Tree", "트리"},
    {"Properties", "속성"},
    {"Source", "소스"},
    {"Problems", "문제"},
    {"Library", "라이브러리"},
    {"Values", "값"},
    {"Preview", "미리보기"},
};

bool g_korean = true;

const std::unordered_map<std::string_view, const char*>& koreanMap() {
    static const std::unordered_map<std::string_view, const char*> map = [] {
        std::unordered_map<std::string_view, const char*> m;
        for (const Pair& p : kKorean) m.emplace(p.en, p.ko);
        return m;
    }();
    return map;
}

std::filesystem::file_time_type g_settingsTime{};
std::chrono::steady_clock::time_point g_nextCheck{};

std::filesystem::file_time_type settingsTime() {
    std::error_code ec;
    const std::string path = mkcontract::userPath(mkcontract::kEditorSettings);
    if (path.empty()) return {};
    const auto t = std::filesystem::last_write_time(std::filesystem::path(std::u8string(path.begin(), path.end())), ec);
    return ec ? std::filesystem::file_time_type{} : t;
}

} // namespace

void setLanguage(const std::string& code) { g_korean = code != "en"; }

const char* language() { return g_korean ? "ko" : "en"; }

const char* tr(const char* english) {
    if (!g_korean) return english;
    const auto& map = koreanMap();
    const auto it = map.find(english);
    return it == map.end() ? english : it->second;
}

std::string trf(const char* english, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, english);
    std::vsnprintf(buffer, sizeof(buffer), tr(english), args);
    va_end(args);
    return buffer;
}

bool hasTranslation(const char* english) { return koreanMap().count(english) != 0; }

void loadLanguage() {
    const std::string code = mkcontract::editorSetting("language");
    setLanguage(code.empty() ? "ko" : code);
    g_settingsTime = settingsTime();
}

bool saveLanguage(const std::string& code) {
    if (!mkcontract::setEditorSetting("language", code)) return false;
    setLanguage(code);
    g_settingsTime = settingsTime();
    return true;
}

bool followLanguage() {
    const auto now = std::chrono::steady_clock::now();
    if (now < g_nextCheck) return false;
    g_nextCheck = now + std::chrono::seconds(1);
    const auto t = settingsTime();
    if (t == g_settingsTime) return false;
    const std::string before = language();
    loadLanguage();
    return before != language();
}

} // namespace mkui::tool
