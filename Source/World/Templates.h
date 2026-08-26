#pragma once
#include "../Assets/Properties.h"
#include <map>
#include <string>

namespace painful {

// Entity instances are thin: a position plus a BaseObj naming a template under
// LScripts/Templates. Templates themselves chain through BaseObj, so resolving
// a property means walking that chain until someone declares it.
//
// Note templates do NOT use the "o." prefix that instances use - each file
// prefixes its own name - which is why Properties accepts any identifier.
class TemplateCache {
public:
    // templatesRoot is Data_Extracted/LScripts/Templates.
    bool Init(const std::string& templatesRoot);

    // 43 levels ship their own Levels/<name>/Templates directory, whose files
    // SHADOW the global ones for that level only - two levels can each define
    // a "swieczka.CParticleFX" with different contents. Call this on every
    // level load; it replaces whatever the previous level installed. Passing an
    // empty path just clears the overlay.
    void SetLevelOverlay(const std::string& levelTemplatesDir);

    // Looks up a template by file name, e.g. "Bat_Adrian.CActor". The level
    // overlay wins over the global set.
    const Properties* Find(const std::string& name);

    // A placed object's own properties win over its BaseObj chain. Every
    // property follows that precedence, so it lives here rather than being
    // rewritten at each call site - which is how o.Model ended up chain-only in
    // three places at once, silently dropping the seven instances that name
    // their model directly (Swamp's water surface among them).
    std::string ResolveString(const Properties& instance, const std::string& baseObj,
                              const std::string& key);
    double ResolveNumber(const Properties& instance, const std::string& baseObj,
                         const std::string& key, double fallback);

    // Walks the BaseObj chain looking for a property.
    std::string ResolveString(const std::string& templateName, const std::string& key);
    double ResolveNumber(const std::string& templateName, const std::string& key, double fallback);
    bool ResolveBool(const std::string& templateName, const std::string& key, bool fallback);
    // True when any template in the chain declares the key at all.
    bool ResolveHas(const std::string& templateName, const std::string& key);

    size_t indexed() const { return index_.size(); }

private:
    std::map<std::string, std::string> index_;      // lowercase name -> path
    std::map<std::string, std::string> overlay_;    // the current level's own
    std::map<std::string, Properties> loaded_;      // lowercase name -> parsed
    std::map<std::string, Properties> overlayLoaded_;
};

} // namespace painful
