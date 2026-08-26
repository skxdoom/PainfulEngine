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

    // Looks up a template by file name, e.g. "Bat_Adrian.CActor".
    const Properties* Find(const std::string& name);

    // Walks the BaseObj chain looking for a property.
    std::string ResolveString(const std::string& templateName, const std::string& key);
    double ResolveNumber(const std::string& templateName, const std::string& key, double fallback);
    bool ResolveBool(const std::string& templateName, const std::string& key, bool fallback);
    // True when any template in the chain declares the key at all.
    bool ResolveHas(const std::string& templateName, const std::string& key);

    size_t indexed() const { return index_.size(); }

private:
    std::map<std::string, std::string> index_;      // lowercase name -> path
    std::map<std::string, Properties> loaded_;      // lowercase name -> parsed
};

} // namespace painful
