#include "Internal.hpp"

namespace classifier_internal {
  namespace {
    bool IsWordChar(const char c) {
      return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    }

    bool IsAlpha(const char c) {
      return std::isalpha(static_cast<unsigned char>(c)) != 0;
    }

    /// Advances past a name of word characters and any of `extra`.
    size_t SkipName(const std::string_view s, size_t i, const std::string_view extra) {
      while (i < s.size() && (IsWordChar(s[i]) || extra.find(s[i]) != std::string_view::npos))
        i++;
      return i;
    }

    /// https:// with nothing after the scheme.
    bool IsSchemeStub(const std::string_view t) {
      return t.size() > 3 && t.ends_with("://") && IsAlpha(t.front()) &&
             SkipName(t, 0, "+.-") == t.size() - 3;
    }

    /// &gt; &laquo; &#160; &#x1F;, alone or in a row.
    bool IsHTMLEntities(const std::string_view t) {
      size_t i = 0;
      int entities = 0;
      while (i < t.size()) {
        if (std::isspace(static_cast<unsigned char>(t[i]))) {
          i++;
          continue;
        }
        if (t[i] != '&')
          return false;
        const bool numeric = t.substr(i + 1).starts_with('#');
        if (!numeric && (i + 1 >= t.size() || !IsAlpha(t[i + 1])))
          return false;
        const size_t end = SkipName(t, numeric ? i + 2 : i + 1, "");
        if (end == i + 1 || end >= t.size() || t[end] != ';')
          return false;
        entities++;
        i = end + 1;
      }
      return entities > 0;
    }

    /// <name>, </name>, <name/>, <subscription-id>.
    bool IsTag(std::string_view t) {
      if (t.size() < 3 || t.front() != '<' || t.back() != '>')
        return false;
      t = t.substr(1, t.size() - 2);
      if (t.starts_with('/'))
        t.remove_prefix(1);
      if (t.ends_with('/'))
        t.remove_suffix(1);
      return !t.empty() && (IsAlpha(t.front()) || t.front() == '_') && SkipName(t, 0, "-:.") == t.size();
    }

    /// Go struct tags: json:"name,omitempty" yaml:"name".
    bool IsStructTag(const std::string_view t) {
      size_t i = 0;
      int tags = 0;
      while (i < t.size()) {
        if (t[i] == ' ') {
          i++;
          continue;
        }
        const size_t key = SkipName(t, i, "-.");
        if (key == i || !IsAlpha(t[i]) || key + 1 >= t.size() || t[key] != ':' || t[key + 1] != '"')
          return false;
        const size_t close = t.find('"', key + 2);
        if (close == std::string_view::npos)
          return false;
        tags++;
        i = close + 1;
      }
      return tags > 0;
    }

    /// -r, --name=, -Xlog:vtables=debug, --diff-filter=ACMRT.
    bool IsCommandLineFlag(const std::string_view t) {
      size_t i = t.starts_with("--") ? 2 : t.starts_with('-') ? 1 : 0;
      if (i == 0 || i >= t.size() || !IsAlpha(t[i]))
        return false;
      i = SkipName(t, i, "-.:");
      return i == t.size() || t[i] == '=';
    }

    bool StartsWithAny(const std::string_view t, std::initializer_list<std::string_view> prefixes) {
      return std::ranges::any_of(prefixes, [&](const std::string_view p) { return t.starts_with(p); });
    }

    bool IsHTMLTagName(const std::string_view name) {
      static constexpr std::string_view kTags[] = {
        "a", "article", "aside", "audio", "b", "blockquote", "body", "br", "button", "canvas",
        "code", "dd", "details", "div", "dl", "dt", "em", "fieldset", "figcaption", "figure",
        "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr", "html", "i",
        "iframe", "img", "input", "label", "legend", "li", "main", "nav", "ol", "option", "p",
        "path", "pre", "section", "select", "small", "span", "strong", "summary", "svg",
        "table", "tbody", "td", "textarea", "th", "thead", "tr", "ul", "video",
      };
      return std::ranges::binary_search(kTags, name);
    }

    /// One compound CSS selector: div, .btn-default, #id, a:hover, input[type="x"].
    /// A token that starts with a letter must start with an HTML tag name.
    bool IsSelectorToken(const std::string_view t) {
      if (t == ">" || t == "+" || t == "~" || t == "*")
        return true;
      if (t.empty() || !(IsAlpha(t.front()) || t.front() == '.' || t.front() == '#' || t.front() == '['))
        return false;
      if (IsAlpha(t.front()) && !IsHTMLTagName(t.substr(0, std::min(t.find_first_of(".#:["), t.size()))))
        return false;
      return std::ranges::all_of(t, [](const char c) {
        return IsWordChar(c) || std::string_view("-.#:[]=\"'()*,^$|~").find(c) != std::string_view::npos;
      });
    }

    /// CSS selectors and values: :not(h1), [data-toggle="x"], [&:has(x)]:rounded,
    /// translateX(, rgb(0, 0, 255), #e9c7cd, #navbar > .try-again.
    bool IsCSS(const std::string_view t) {
      if (t.starts_with(':')) {
        auto name = t.substr(t.starts_with("::") ? 2 : 1);
        return StartsWithAny(name, {"not(", "is(", "where(", "has(", "hover", "focus", "active", "visited",
                                    "before", "after", "first-", "last-", "nth-", "only-", "checked",
                                    "disabled", "enabled", "root", "empty", "placeholder", "selection"});
      }
      if (t.starts_with("[&") || t.starts_with("[data-") || t.starts_with("[aria-"))
        return true;
      /// [type="text"], [href^=http]: an attribute test, so a bare [NAME] such as a
      /// log prefix is not one.
      if (t.starts_with('[') && t.ends_with(']') && t.find(' ') == std::string_view::npos) {
        const size_t name = SkipName(t, 1, "-");
        return name > 1 && IsAlpha(t[1]) && name < t.size() && std::string_view("=~|^$*").find(t[name]) != std::string_view::npos;
      }
      if (StartsWithAny(t, {"-moz-", "-webkit-", "-ms-"}))
        return true;
      if (StartsWithAny(t, {"translate(", "translateX(", "translateY(", "translateZ(", "translate3d(",
                            "rotate(", "rotateX(", "rotateY(", "rotateZ(", "scale(", "scaleX(", "scaleY(",
                            "skew(", "skewX(", "skewY(", "matrix(", "matrix3d(", "rgb(", "rgba(", "hsl(",
                            "hsla(", "calc(", "var(--", "linear-gradient(", "radial-gradient(",
                            "conic-gradient(", "cubic-bezier("}))
        return true;

      /// A selector list: two or more compound selectors, one of them a class,
      /// id, or attribute selector (.widget-title h3, #nav > .item).
      int tokens = 0;
      bool marked = false;
      size_t pos = 0;
      while (pos < t.size()) {
        const size_t end = std::min(t.find(' ', pos), t.size());
        const auto token = t.substr(pos, end - pos);
        pos = end + 1;
        if (token.empty())
          continue;
        if (!IsSelectorToken(token))
          return false;
        tokens++;
        if (token.front() == '.' || token.front() == '#' || token.front() == '[')
          marked = true;
      }
      return tokens >= 2 && marked;
    }

    /// Scoped packages and sigil names: @azure/identity, @type, $injector, $.output.
    bool IsSigilName(const std::string_view t) {
      size_t i = 0;
      while (i < t.size() && (t[i] == '@' || t[i] == '$'))
        i++;
      if (i == 0 || i >= t.size() || i > 2)
        return false;
      return std::ranges::any_of(t, IsAlpha) && SkipName(t, i, "-./$") == t.size();
    }

    /// Calls and signatures: getFilePath(), load(String), std::move(x), the open
    /// call Symbol(, and the variable reference $(NAME).
    bool IsCall(const std::string_view t) {
      if (t.size() > 3 && t.starts_with("$(") && t.ends_with(')') && SkipName(t, 2, "") == t.size() - 1)
        return true;
      /// A Go method expression: (*UnixConn).CloseRead, (testing.TB).Errorf.
      if (t.starts_with('(')) {
        const size_t type = SkipName(t, t.starts_with("(*") ? 2 : 1, ".");
        return type + 2 < t.size() && t[type] == ')' && t[type + 1] == '.' && IsAlpha(t[type + 2]) &&
               SkipName(t, type + 2, "") == t.size();
      }
      const size_t i = t.front() == '.' || t.front() == '$' ? 1 : 0;
      if (i >= t.size() || !(IsAlpha(t[i]) || t[i] == '_'))
        return false;
      const size_t name = SkipName(t, i, ".$:<>");
      if (name >= t.size() || t[name] != '(')
        return false;
      if (name + 1 == t.size())
        return true;
      const size_t close = t.find_first_of("()", name + 1);
      if (close == std::string_view::npos || t[close] != ')')
        return false;
      /// A statement with literal arguments (console.log(1);, println("x");) is code.
      const auto args = t.substr(name + 1, close - name - 1);
      if (t.ends_with(';') || args.find_first_of("\"'") != std::string_view::npos ||
          (!args.empty() && std::isdigit(static_cast<unsigned char>(args.front()))))
        return false;
      return SkipName(t, close + 1, ".[]:") == t.size();
    }

    /// JVM type descriptors: (II)I, ()V, Lkotlin/Unit;, [Lorg/x/Y;.
    bool IsJVMDescriptor(const std::string_view t) {
      auto type = [&](size_t &i) {
        while (i < t.size() && t[i] == '[')
          i++;
        if (i >= t.size())
          return false;
        if (t[i] == 'L') {
          const size_t semi = t.find(';', i);
          if (semi == std::string_view::npos || semi == i + 1 || SkipName(t, i + 1, "/$") != semi)
            return false;
          i = semi + 1;
          return true;
        }
        if (std::string_view("BCDFIJSZV").find(t[i]) == std::string_view::npos)
          return false;
        i++;
        return true;
      };
      size_t i = 0;
      if (t.starts_with('(')) {
        i = 1;
        while (i < t.size() && t[i] != ')')
          if (!type(i))
            return false;
        if (i >= t.size())
          return false;
        i++;
        return type(i) && i == t.size();
      }
      const bool reference = t.find(';') != std::string_view::npos || t.starts_with('[');
      return reference && type(i) && i == t.size();
    }

    /// Lists of names: audio,video, top|middle|bottom, REQ,Fixnum,a.
    bool IsNameList(const std::string_view t) {
      int items = 0;
      int names = 0;
      size_t pos = 0;
      while (pos <= t.size()) {
        const size_t end = std::min(t.find_first_of(",;|", pos), t.size());
        const auto item = t.substr(pos, end - pos);
        if (item.empty() || SkipName(item, 0, ".-") != item.size())
          return false;
        items++;
        if (IsAlpha(item.front()) || item.front() == '_')
          names++;
        if (end == t.size())
          break;
        pos = end + 1;
      }
      return items >= 2 && names > 0;
    }
  } // namespace

  ClassificationResult DetectNotation(const std::string_view s) {
    const auto t = Trim(s);
    if (t.size() < 2)
      return {LanguageEnum::Unknown, 0.0f};
    if (IsSchemeStub(t))
      return {LanguageEnum::URL, 0.85f};
    if (IsHTMLEntities(t))
      return {LanguageEnum::HTML, 0.90f};
    if (IsTag(t))
      return {LanguageEnum::XML, 0.85f};
    if (IsStructTag(t))
      return {LanguageEnum::Go, 0.90f};
    if (IsCSS(t))
      return {LanguageEnum::CSS, 0.85f};
    if (t.find_first_of(" \t\n\r") != std::string_view::npos)
      return {LanguageEnum::Unknown, 0.0f};
    if (IsCommandLineFlag(t))
      return {LanguageEnum::Shell, 0.85f};
    if (IsSigilName(t) || IsCall(t) || IsJVMDescriptor(t) || IsNameList(t))
      return {LanguageEnum::IdentifierLike, 0.85f};
    return {LanguageEnum::Unknown, 0.0f};
  }
} // namespace classifier_internal
