module;
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

export module win32_ui_editor.importparser;

import std;
import win32_ui_editor.model;

//
// Parser:
// 1. Strip comments (// and /* */)
// 2. Tokenize (identifier, number, string, symbol)
// 3. Find CreateWindowEx*(...) calls (multi-line allowed)
// 4. Parse arguments: exStyle, class, text, style, x, y, w, h, parent, menu, inst, param
// 5. Extract class name / text / style expression as raw strings
// 6. Evaluate simple integer expressions symbolically
// 7. Create ControlDef objects
// 8. Infer container hierarchy (parentIndex/isContainer) from geometry and type
//

namespace win32_ui_editor::importparser::detail
{
    using std::string;
    using std::wstring;
    using std::vector;
    using win32_ui_editor::model::ControlDef;
    using win32_ui_editor::model::ControlType;
    using win32_ui_editor::model::is_container_type;

    // =====================================================================
    // Utility: Strip comments
    // =====================================================================
    string strip_comments(const string& input)
    {
        string out;
        out.reserve(input.size());

        enum { NORMAL, SLASH, LINE, BLOCK } state = NORMAL;

        for (size_t i = 0; i < input.size(); ++i)
        {
            char c = input[i];
            char n = (i + 1 < input.size() ? input[i + 1] : 0);

            switch (state)
            {
            case NORMAL:
                if (c == '/')
                {
                    state = SLASH;
                }
                else
                {
                    out.push_back(c);
                }
                break;

            case SLASH:
                if (c == '/' && n != '*')
                {
                    state = LINE;
                    ++i;
                }
                else if (c == '*' && n != '/')
                {
                    state = BLOCK;
                    ++i;
                }
                else
                {
                    out.push_back('/');
                    out.push_back(c);
                    state = NORMAL;
                }
                break;

            case LINE:
                if (c == '\n')
                {
                    out.push_back('\n');
                    state = NORMAL;
                }
                break;

            case BLOCK:
                if (c == '*' && n == '/')
                {
                    state = NORMAL;
                    ++i;
                }
                break;
            }
        }

        return out;
    }

    // =====================================================================
    // Tokenizer
    // =====================================================================
    enum class TokKind
    {
        Identifier,
        Number,
        String,
        Symbol,
        End
    };

    struct Token
    {
        TokKind kind{};
        string  text{};
    };

    class Tokenizer
    {
        const string& src;
        size_t pos = 0;

    public:
        explicit Tokenizer(const string& s) : src(s) {}

        bool eof() const { return pos >= src.size(); }

        char peek() const { return eof() ? 0 : src[pos]; }

        char get()
        {
            if (eof()) return 0;
            return src[pos++];
        }

        static bool isIdentStart(char c)
        {
            return std::isalpha((unsigned char)c) || c == '_' || c == '$';
        }

        static bool isIdentChar(char c)
        {
            return std::isalnum((unsigned char)c) || c == '_' || c == '$';
        }

        Token next()
        {
            while (!eof() && std::isspace((unsigned char)peek()))
                ++pos;

            if (eof()) return { TokKind::End, "" };

            char c = peek();

            // Identifier
            if (isIdentStart(c))
            {
                string t;
                t.push_back(get());
                while (!eof() && isIdentChar(peek()))
                    t.push_back(get());
                return { TokKind::Identifier, std::move(t) };
            }

            // Number
            if (std::isdigit((unsigned char)c))
            {
                string t;
                t.push_back(get());
                while (!eof() && (std::isalnum((unsigned char)peek()) || peek() == 'x' || peek() == 'X'))
                    t.push_back(get());
                return { TokKind::Number, std::move(t) };
            }

            // String literal (single or double quoted)
            if (c == '"' || c == '\'')
            {
                char quote = get();
                string t;
                t.push_back(quote);

                while (!eof())
                {
                    char d = get();
                    t.push_back(d);
                    if (d == quote) break;
                    if (d == '\\' && !eof())
                        t.push_back(get());
                }
                return { TokKind::String, std::move(t) };
            }

            // Symbol
            string t;
            t.push_back(get());
            return { TokKind::Symbol, std::move(t) };
        }
    };

    // =====================================================================
    // Expression evaluator (symbolic)
    // =====================================================================
    struct EvalContext
    {
        int width = 1100;
        int height = 700;
        std::unordered_map<string, int> vars;

        int getVar(const string& v) const
        {
            if (v == "width")  return width;
            if (v == "height") return height;
            auto it = vars.find(v);
            return (it == vars.end() ? 0 : it->second);
        }
    };

    class ExprParser
    {
        vector<Token>& t;
        size_t i = 0;
        EvalContext& ctx;

        Token& cur() { return t[i]; }
        bool atEnd() { return t[i].kind == TokKind::End; }
        void bump() { if (!atEnd()) ++i; }

        int parsePrimary()
        {
            if (cur().kind == TokKind::Number)
            {
                int base = 10;
                const string& s = cur().text;
                if (s.starts_with("0x") || s.starts_with("0X")) base = 16;
                int val = std::stoi(s, nullptr, base);
                bump();
                return val;
            }
            if (cur().kind == TokKind::Identifier)
            {
                string v = cur().text;
                bump();
                return ctx.getVar(v);
            }
            if (cur().text == "(")
            {
                bump();
                int v = parseExpr();
                if (cur().text == ")") bump();
                return v;
            }
            // Fallback
            bump();
            return 0;
        }

        int parseMul()
        {
            int lhs = parsePrimary();
            while (cur().text == "*" || cur().text == "/")
            {
                string op = cur().text;
                bump();
                int rhs = parsePrimary();
                if (op == "*")      lhs *= rhs;
                else if (op == "/") lhs = (rhs == 0 ? lhs : lhs / rhs);
            }
            return lhs;
        }

        int parseExpr()
        {
            int lhs = parseMul();
            while (cur().text == "+" || cur().text == "-")
            {
                string op = cur().text;
                bump();
                int rhs = parseMul();
                if (op == "+") lhs += rhs;
                else           lhs -= rhs;
            }
            return lhs;
        }

    public:
        ExprParser(vector<Token>& tokens, EvalContext& c)
            : t(tokens), ctx(c) {
        }

        int eval() { return parseExpr(); }
    };

    // =====================================================================
    // Helper: Extract tokens between parentheses (depth-aware)
    // =====================================================================
    vector<Token> extractArguments(Tokenizer& tz)
    {
        vector<Token> args;
        int  depth = 0;
        bool started = false;

        for (;;)
        {
            Token k = tz.next();
            if (k.kind == TokKind::End)
                break;

            if (k.text == "(")
            {
                depth++;
                if (!started)
                {
                    started = true;
                    continue;
                }
            }
            else if (k.text == ")")
            {
                depth--;
                if (depth == 0) break;
            }

            if (started)
                args.push_back(k);
        }

        return args;
    }

    // =====================================================================
    // Split tokens into argument lists by commas (top-level only)
    // =====================================================================
    vector<vector<Token>> splitByComma(const vector<Token>& toks)
    {
        vector<vector<Token>> out;
        vector<Token> current;
        int depth = 0;

        for (auto& t : toks)
        {
            if (t.text == "(") depth++;
            if (t.text == ")") depth--;

            if (t.text == "," && depth == 0)
            {
                out.push_back(current);
                current.clear();
            }
            else
            {
                current.push_back(t);
            }
        }

        if (!current.empty())
            out.push_back(current);

        return out;
    }

    // =====================================================================
    // Convert token list to string
    // =====================================================================
    string tokensToString(const vector<Token>& v)
    {
        string s;
        for (auto& t : v)
        {
            if (!s.empty()) s.push_back(' ');
            s += t.text;
        }
        return s;
    }

    // =====================================================================
    // Infer container hierarchy from geometry
    // =====================================================================
    void build_container_hierarchy(vector<ControlDef>& controls)
    {
        // Mark containers
        for (auto& c : controls)
        {
            c.isContainer = is_container_type(c.type);
            c.parentIndex = -1;
        }

        const int n = (int)controls.size();
        for (int i = 0; i < n; ++i)
        {
            auto& child = controls[i];
            RECT  rcChild = child.rect;

            int  bestIdx = -1;
            LONG bestArea = 0;

            for (int j = 0; j < n; ++j)
            {
                if (j == i)
                    continue;

                auto& parent = controls[j];
                if (!parent.isContainer)
                    continue;

                RECT rcPar = parent.rect;

                // Simple containment check: child fully inside parent
                if (rcPar.left <= rcChild.left &&
                    rcPar.top <= rcChild.top &&
                    rcPar.right >= rcChild.right &&
                    rcPar.bottom >= rcChild.bottom)
                {
                    LONG w = rcPar.right - rcPar.left;
                    LONG h = rcPar.bottom - rcPar.top;
                    LONG area = (w > 0 && h > 0) ? (w * h) : 0;

                    // Pick smallest container that still contains the child
                    if (bestIdx == -1 || area < bestArea)
                    {
                        bestIdx = j;
                        bestArea = area;
                    }
                }
            }

            child.parentIndex = bestIdx; // -1 if no container
        }
    }

    // =====================================================================
    // Extract UI controls from code
    // =====================================================================
    vector<ControlDef> parseFromCode(const string& originalInput)
    {
        // Pre-clean
        string input = strip_comments(originalInput);

        // Tokenize whole file
        Tokenizer tz(input);
        vector<ControlDef> controls;

        // Symbolic evaluation context
        EvalContext ctx;
        ctx.vars["tab_height"] = 28;
        ctx.vars["detail_height"] = (ctx.height - 28) / 3;
        ctx.vars["list_height"] = (ctx.height - 28) - ctx.vars["detail_height"];

        // Main loop: find CreateWindowEx*( ) calls
        for (;;)
        {
            Token tok = tz.next();
            if (tok.kind == TokKind::End)
                break;

            if (tok.kind == TokKind::Identifier &&
                (tok.text == "CreateWindowEx" ||
                    tok.text == "CreateWindowExW" ||
                    tok.text == "CreateWindowExA"))
            {
                // Extract arguments
                auto argsTokens = extractArguments(tz);
                auto parts = splitByComma(argsTokens);

                // Must be at least 12 args
                if (parts.size() < 12)
                    continue;

                auto toStr = [&](int idx) { return tokensToString(parts[idx]); };

                string exStyleExpr = toStr(0);
                string classExpr = toStr(1);
                string textExpr = toStr(2);
                string styleExpr = toStr(3);
                string xExpr = toStr(4);
                string yExpr = toStr(5);
                string wExpr = toStr(6);
                string hExpr = toStr(7);
                string parentExpr = toStr(8);
                string menuExpr = toStr(9);

                (void)exStyleExpr;
                (void)parentExpr;

                // Evaluate numeric expressions
                auto tokenizeExpr = [&](const string& e) {
                    Tokenizer ttz(e);
                    vector<Token> tt;
                    for (;;)
                    {
                        Token tk = ttz.next();
                        if (tk.kind == TokKind::End) break;
                        tt.push_back(tk);
                    }
                    tt.push_back({ TokKind::End, "" });
                    return tt;
                    };

                auto evalExpr = [&](const string& e, int fallback) {
                    auto tt = tokenizeExpr(e);
                    ExprParser ep(tt, ctx);
                    try { return ep.eval(); }
                    catch (...) { return fallback; }
                    };

                int X = evalExpr(xExpr, 10);
                int Y = evalExpr(yExpr, 10);
                int W = evalExpr(wExpr, 80);
                int H = evalExpr(hExpr, 24);

                // Extract ID if present
                wstring idName;
                int     id = -1;
                {
                    if (menuExpr.find("HMENU") != string::npos)
                    {
                        size_t p = menuExpr.find("ID_");
                        if (p != string::npos)
                        {
                            size_t q = p;
                            while (q < menuExpr.size() &&
                                (std::isalnum((unsigned char)menuExpr[q]) ||
                                    menuExpr[q] == '_'))
                            {
                                ++q;
                            }

                            idName = wstring(menuExpr.begin() + p, menuExpr.begin() + q);
                        }
                        id = 1000 + (int)controls.size();
                    }
                }

                // Extract class name
                auto parseClass = [&](const string& cls) -> wstring {
                    if (cls.starts_with("WC_"))
                        return wstring(cls.begin(), cls.end()); // macro name
                    if (cls.size() >= 2 && cls.front() == '"' && cls.back() == '"')
                        return wstring(cls.begin() + 1, cls.end() - 1);
                    return wstring(cls.begin(), cls.end());
                    };

                wstring clsName = parseClass(classExpr);

                // Extract text
                auto parseText = [&](const string& s) -> wstring {
                    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                        return wstring(s.begin() + 1, s.end() - 1);
                    return wstring(s.begin(), s.end());
                    };

                wstring textW = parseText(textExpr);

                // Determine control type
                auto classify = [&](const wstring& cn, const string& style) {
                    wstring u;
                    u.reserve(cn.size());
                    for (auto ch : cn)
                        u.push_back((wchar_t)std::towupper(ch));

                    auto styleHas = [&](const char* token) {
                        return style.find(token) != string::npos;
                        };

                    if (u.find(L"STATIC") != wstring::npos) return ControlType::Static;
                    if (u.find(L"EDIT") != wstring::npos) return ControlType::Edit;
                    if (u.find(L"LISTBOX") != wstring::npos) return ControlType::ListBox;
                    if (u.find(L"COMBOBOX") != wstring::npos) return ControlType::ComboBox;
                    if (u.find(L"LISTVIEW") != wstring::npos) return ControlType::ListView;
                    if (u.find(L"TAB") != wstring::npos) return ControlType::Tab;
                    if (u.find(L"TOOLTIP") != wstring::npos) return ControlType::Tooltip;

                    // BUTTON family: disambiguate by style bits
                    if (u.find(L"BUTTON") != wstring::npos)
                    {
                        if (styleHas("BS_GROUPBOX"))       return ControlType::GroupBox;
                        if (styleHas("BS_AUTOCHECKBOX") ||
                            styleHas("BS_CHECKBOX"))       return ControlType::Checkbox;
                        if (styleHas("BS_AUTORADIOBUTTON") ||
                            styleHas("BS_RADIOBUTTON"))    return ControlType::Radio;
                        return ControlType::Button;
                    }

                    // Fallback
                    return ControlType::Static;
                    };

                ControlDef cd{};
                cd.text = textW;
                cd.className = clsName;
                cd.styleExpr = wstring(styleExpr.begin(), styleExpr.end());
                cd.idName = idName;
                cd.id = id;

                cd.rect.left = X;
                cd.rect.top = Y;
                cd.rect.right = X + W;
                cd.rect.bottom = Y + H;

                cd.type = classify(clsName, styleExpr);

                controls.push_back(cd);
            }
        }

        // Build container hierarchy
        build_container_hierarchy(controls);

        // Normalize IDs/styles
        for (size_t i = 0; i < controls.size(); ++i)
        {
            auto& c = controls[i];
            if (c.id <= 0)
                c.id = 1000 + static_cast<int>(i);
            if (c.styleExpr.empty())
                c.styleExpr = win32_ui_editor::model::default_style_expr(c.type);
        }

        return controls;
    }

} // namespace detail

// =====================================================================
// Exported entrypoint
// =====================================================================
export namespace win32_ui_editor::importparser
{
    using std::string;
    using std::vector;
    using win32_ui_editor::model::ControlDef;

    export vector<ControlDef> parse_controls_from_code(const string& code)
    {
        return detail::parseFromCode(code);
    }
}
