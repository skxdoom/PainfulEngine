// What the renderer will be handed: materials, emitters and billboards.
#include "Commands.h"

int ShadersCmd(const char* dataRoot, const char* single) {
    ShaderLibrary lib;
    if (!lib.LoadDirectory(std::string(dataRoot) + "/Shaders/Scripts")) {
        for (const std::string& e : lib.errors()) LogInfo("  error: %s", e.c_str());
        if (lib.size() == 0) return 2;
    }
    LogInfo("%zu shader definitions parsed", lib.size());
    for (const std::string& e : lib.errors()) LogInfo("  error: %s", e.c_str());

    // A name matches every variant of that name, and a file name matches every
    // shader the file defines: one shader is declared once per hardware tier,
    // and reading only the first hides what the others say.
    if (single && single[0]) {
        int shown = 0;
        for (const ShaderDef& def : lib.all()) {
            if (def.name != single && def.sourceFile != single) continue;
            ++shown;
            LogInfo("shader %s  variant=%s  (%s)", def.name.c_str(),
                    def.variant.empty() ? "any" : def.variant.c_str(),
                    def.sourceFile.c_str());
            for (size_t p = 0; p < def.passes.size(); ++p) {
                LogInfo("  pass %zu:", p);
                for (const auto& kv : def.passes[p].keys)
                    LogInfo("    %-14s %s", kv.first.c_str(), kv.second.c_str());
            }
        }
        if (shown == 0) { LogInfo("'%s' not found", single); return 2; }
        return 0;
    }

    // Which file declares what, so a shader can be found without knowing its
    // name first - the scripts are inside a .pak with a hashed table of
    // contents, so there is no directory listing to read.
    std::map<std::string, std::vector<std::string>> byFile;
    for (const ShaderDef& d : lib.all()) byFile[d.sourceFile].push_back(d.name);
    LogInfo("shader scripts:");
    for (const auto& kv : byFile) {
        std::string names;
        for (const std::string& n : kv.second) {
            if (!names.empty()) names += " ";
            names += n;
        }
        LogInfo("  %-22s %s", kv.first.c_str(), names.c_str());
    }
    // Summary: names per variant, and every distinct vshader/fshader/fx used.
    std::map<std::string, int> variants;
    std::map<std::string, int> programs;
    for (const ShaderDef& d : lib.all()) {
        ++variants[d.variant.empty() ? "any" : d.variant];
        for (const ShaderPass& p : d.passes) {
            for (const char* key : {"vshader", "fshader", "fx"}) {
                std::string v = p.Get(key);
                if (!v.empty()) ++programs[std::string(key) + " " + v];
            }
        }
    }
    for (const auto& kv : variants) LogInfo("  %-6s %d", kv.first.c_str(), kv.second);
    LogInfo("programs referenced:");
    for (const auto& kv : programs) LogInfo("  %3dx  %s", kv.second, kv.first.c_str());
    return 0;
}


int ParticlesCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed: %s", level.error().c_str()); return 2; }
    TemplateCache templates;
    templates.Init(std::string(dataRoot) + "/LScripts/Templates");
    templates.SetLevelOverlay(std::string(levelDir) + "/Templates");
    EmitterLibrary library;
    library.Init(std::string(dataRoot) + "/Scripts");
    LogInfo("library: %zu emitters, %zu effects", library.indexedEmitters(),
            library.indexedEffects());

    static const char* kBlendName[12] = {"none", "alpha", "add", "modulate", "filter",
                                         "translucent", "invmodulate", "subtract",
                                         "revsubtract", "desttranslucent", "destalpha",
                                         "modulate2x"};
    std::map<std::string, size_t> byEffect;
    size_t placed = 0, unresolved = 0, budget = 0;
    for (const Entity& e : level.entities()) {
        if (e.type != "CParticleFX") continue;
        ++placed;
        std::string effect = e.props.String("Effect", "");
        if (effect.empty()) effect = templates.ResolveString(e.baseObj, "Effect");
        if (effect.empty()) effect = "Default";
        byEffect[effect]++;
        if (!library.Effect(effect)) ++unresolved;
    }
    LogInfo("%zu CParticleFX placed, %zu distinct effects, %zu unresolved", placed,
            byEffect.size(), unresolved);

    for (const auto& kv : byEffect) {
        const ParticleFxDef* fx = library.Effect(kv.first);
        LogInfo("  %-28s x%-4zu %s", kv.first.c_str(), kv.second,
                fx ? "" : "(UNRESOLVED)");
        if (!fx) continue;
        for (const ParticleFxDef::Ref& ref : fx->emitters) {
            const EmitterParams* p = library.Emitter(ref.file);
            if (!p) { LogInfo("      %-24s (UNRESOLVED)", ref.file.c_str()); continue; }
            budget += static_cast<size_t>(p->maxParticles) * kv.second;
            LogInfo("      %-24s scale %.2f  type %d  blend %-11s  max %4d  rate %6.1f/s  "
                    "life %.2f-%.2f  tex %s",
                    ref.file.c_str(), ref.scale, p->type,
                    (p->blendMode >= 0 && p->blendMode < 12) ? kBlendName[p->blendMode] : "?",
                    p->maxParticles,
                    p->spawnInterval > 0.f ? 1.f / p->spawnInterval : 0.f,
                    p->lifeMin, p->lifeMax, p->texture.c_str());
        }
    }
    LogInfo("worst-case particle budget for this level: %zu", budget);
    for (const std::string& err : library.errors()) LogWarn("  %s", err.c_str());
    return 0;
}

// Resolves every CBillboard the level places through its template chain and
// reports what came out, so the corona parameters can be checked without a
// window. Also builds the collision BVH and times it, since coronas are the
// first thing to depend on it.
int BillboardsCmd(const char* levelDir, const char* dataRoot) {
    Level level;
    if (!level.Load(levelDir, dataRoot)) { LogInfo("failed: %s", level.error().c_str()); return 2; }
    TemplateCache templates;
    templates.Init(std::string(dataRoot) + "/LScripts/Templates");
    templates.SetLevelOverlay(std::string(levelDir) + "/Templates");
    TextureCache textures;
    textures.Init(std::string(dataRoot) + "/Textures", false);

    static const char* kBlendName[5] = {"none", "alpha", "add", "filter", "translucent"};
    struct Row {
        size_t count = 0, coronas = 0;
        std::string texture, resolved;
        float size = 0, minSize = 0, alpha = 0, off = 0;
        int blend = 0;
    };
    std::map<std::string, Row> byBase;
    size_t total = 0, coronas = 0, missingTex = 0;

    for (const Entity& e : level.entities()) {
        if (e.type != "CBillboard") continue;
        ++total;

        // Mirrors BillboardRenderer::Build's walk: instance first, then the
        // BaseObj chain.
        std::vector<const Properties*> chain{&e.props};
        std::string current = e.baseObj;
        for (int d = 0; d < 16 && !current.empty(); ++d) {
            const Properties* p = templates.Find(current);
            if (!p) break;
            chain.push_back(p);
            current = p->String("BaseObj");
        }
        auto find = [&](const char* key) -> const Value* {
            for (const Properties* p : chain) if (const Value* v = p->Find(key)) return v;
            return nullptr;
        };
        auto num = [&](const char* key, float fallback) {
            const Value* v = find(key);
            return v && v->kind == Value::Kind::Number ? float(v->number) : fallback;
        };

        Row& row = byBase[e.baseObj.empty() ? "(none)" : e.baseObj];
        ++row.count;
        const Value* en = find("Corona.Enabled");
        const bool isCorona = en && en->kind == Value::Kind::Bool && en->boolean;
        if (isCorona) { ++row.coronas; ++coronas; }

        const Value* t = find("Texture");
        row.texture = t && t->kind == Value::Kind::String ? t->text : "banka";
        row.size = num("Size", 5.f);
        row.minSize = num("Corona.MinSize", 0.8f);
        row.alpha = num("Alpha", 0.5f);
        row.off = num("Corona.OffDistance", 70.f);
        row.blend = int(num("BlendMode", 1.f));
        row.resolved = textures.Resolve("Particles/" + row.texture, level.name());
        if (row.resolved.empty()) ++missingTex;
    }

    LogInfo("%zu CBillboard placed (%zu coronas), %zu distinct templates", total, coronas,
            byBase.size());
    for (const auto& kv : byBase) {
        const Row& r = kv.second;
        LogInfo("  %-32s x%-4zu %s  tex %-18s %s  size %.1f-%.1f  alpha %.2f  off %.0f  blend %s",
                kv.first.c_str(), r.count, r.coronas ? "corona " : "sprite ", r.texture.c_str(),
                r.resolved.empty() ? "(MISSING)" : "ok", r.minSize, r.size, r.alpha, r.off,
                (r.blend >= 0 && r.blend < 5) ? kBlendName[r.blend] : "?");
    }
    if (missingTex) LogWarn("%zu templates reference a texture that does not resolve", missingTex);

    if (level.mapLoaded()) {
        const auto t0 = std::chrono::steady_clock::now();
        CollisionMesh collision;
        collision.Build(level.map(), level.info().scale);
        const float ms = std::chrono::duration<float, std::milli>(
                             std::chrono::steady_clock::now() - t0).count();
        LogInfo("collision BVH built in %.0f ms", ms);
    }
    return 0;
}

// Poses a model by one of its animations and reports what moved. This is the
// check that CPU skinning is right, without a window and without a screenshot:
// a bind pose and a posed pose have very different bounds, and a model that
// silently failed to skin reports them identical.
//
// It is also the oracle the GPU skinning path will be diffed against, one
// bone at a time - which is the reason CPU skinning was built first at all.
