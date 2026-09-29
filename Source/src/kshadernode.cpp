#include "kshadernode.h"
#include <set>
#include <sstream>
#include <chrono>

namespace kemena
{

// ===========================================================================
// kShaderNode helpers
// ===========================================================================

const char* kShaderNode::typeName(kShaderNodeType t)
{
    switch (t)
    {
        case kShaderNodeType::Anchor:             return "Anchor";
        case kShaderNodeType::Comment:            return "Comment";
        case kShaderNodeType::ConstFloat:         return "Float";
        case kShaderNodeType::ConstVec2:          return "Vec2";
        case kShaderNodeType::ConstVec3:          return "Vec3 / Color";
        case kShaderNodeType::ConstVec4:          return "Vec4 / Color";
        case kShaderNodeType::UVCoord:            return "UV Coord";
        case kShaderNodeType::Time:               return "Time";
        case kShaderNodeType::VertexColor:        return "Vertex Color";
        case kShaderNodeType::WorldPosition:      return "World Position";
        case kShaderNodeType::ViewDirection:      return "View Direction";
        case kShaderNodeType::VertexNormal:       return "Vertex Normal";
        case kShaderNodeType::Texture2D:          return "Texture 2D";
        case kShaderNodeType::TextureCube:        return "Texture Cube";
        case kShaderNodeType::Add:                return "Add";
        case kShaderNodeType::Subtract:           return "Subtract";
        case kShaderNodeType::Multiply:           return "Multiply";
        case kShaderNodeType::Divide:             return "Divide";
        case kShaderNodeType::Dot:                return "Dot Product";
        case kShaderNodeType::Cross:              return "Cross Product";
        case kShaderNodeType::Normalize:          return "Normalize";
        case kShaderNodeType::Length:             return "Length";
        case kShaderNodeType::Clamp:              return "Clamp";
        case kShaderNodeType::Mix:                return "Mix";
        case kShaderNodeType::Pow:                return "Power";
        case kShaderNodeType::Abs:                return "Abs";
        case kShaderNodeType::Floor:              return "Floor";
        case kShaderNodeType::Ceil:               return "Ceil";
        case kShaderNodeType::Fract:              return "Fract";
        case kShaderNodeType::Sqrt:               return "Sqrt";
        case kShaderNodeType::Min:                return "Min";
        case kShaderNodeType::Max:                return "Max";
        case kShaderNodeType::Step:               return "Step";
        case kShaderNodeType::Smoothstep:         return "Smoothstep";
        case kShaderNodeType::OneMinus:           return "One Minus";
        case kShaderNodeType::Split:              return "Split";
        case kShaderNodeType::Combine:            return "Combine";
        case kShaderNodeType::Swizzle:            return "Swizzle";
        case kShaderNodeType::MaterialTiling:     return "Material Tiling";
        case kShaderNodeType::MaterialAmbient:    return "Material Ambient";
        case kShaderNodeType::MaterialDiffuse:    return "Material Diffuse";
        case kShaderNodeType::MaterialSpecular:   return "Material Specular";
        case kShaderNodeType::MaterialShininess:  return "Material Shininess";
        case kShaderNodeType::MaterialMetallic:   return "Material Metallic";
        case kShaderNodeType::MaterialRoughness:  return "Material Roughness";
        case kShaderNodeType::OutputFlat:         return "Output (Flat)";
        case kShaderNodeType::OutputPhong:        return "Output (Phong)";
        case kShaderNodeType::OutputPBR:          return "Output (PBR)";
        default:                            return "Unknown";
    }
}

// ===========================================================================
// kShaderGraph
// ===========================================================================

kShaderNode* kShaderGraph::findNode(int id)
{
    for (auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

const kShaderNode* kShaderGraph::findNode(int id) const
{
    for (const auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

const kShaderLink* kShaderGraph::incomingLink(int nodeId, int pinId) const
{
    for (const auto& l : links)
        if (l.toNode == nodeId && l.toPin == pinId) return &l;
    return nullptr;
}

bool kShaderGraph::isPinConnected(int nodeId, int pinId) const
{
    for (const auto& l : links)
        if ((l.toNode == nodeId && l.toPin == pinId) ||
            (l.fromNode == nodeId && l.fromPin == pinId))
            return true;
    return false;
}

void kShaderGraph::removeLinksByNode(int nodeId)
{
    links.erase(std::remove_if(links.begin(), links.end(),
        [nodeId](const kShaderLink& l)
        { return l.fromNode == nodeId || l.toNode == nodeId; }),
        links.end());
}

void kShaderGraph::removeLinksByPin(int nodeId, int pinId)
{
    links.erase(std::remove_if(links.begin(), links.end(),
        [nodeId, pinId](const kShaderLink& l)
        { return (l.toNode == nodeId && l.toPin == pinId) ||
                 (l.fromNode == nodeId && l.fromPin == pinId); }),
        links.end());
}

// ---------------------------------------------------------------------------
// makeNode: builds a node with correct default pins for each type
// ---------------------------------------------------------------------------
static kShaderPin makePin(int id, const char* name, kPinType type, bool isOutput,
                          float defF = 0.f,
                          float r = 0.f, float g = 0.f, float b = 0.f, float a = 1.f)
{
    kShaderPin p;
    p.id        = id;
    p.name      = name;
    p.type      = type;
    p.isOutput  = isOutput;
    p.defFloat  = defF;
    p.defVec[0] = r; p.defVec[1] = g; p.defVec[2] = b; p.defVec[3] = a;
    return p;
}

kShaderNode kShaderGraph::makeNode(kShaderNodeType type, float x, float y)
{
    kShaderNode n;
    n.id   = newId();
    n.type = type;
    n.name = kShaderNode::typeName(type);
    n.posX = x;
    n.posY = y;

    int pid = newId();

    switch (type)
    {
        // --- Editor utility nodes ---
        case kShaderNodeType::Anchor:
            n.inputs.push_back(makePin(newId(), "In",  kPinType::Float, false));
            n.outputs.push_back(makePin(pid,    "Out", kPinType::Float, true));
            break;

        case kShaderNodeType::Comment:
            n.sizeX   = 320.f;
            n.sizeY   = 180.f;
            n.comment = "Comment";
            break;

        // --- Constants ---
        case kShaderNodeType::ConstFloat:
            n.outputs.push_back(makePin(pid, "Value", kPinType::Float, true));
            break;
        case kShaderNodeType::ConstVec2:
            n.outputs.push_back(makePin(pid, "Value", kPinType::Vec2, true));
            break;
        case kShaderNodeType::ConstVec3:
            n.valueFloat[0] = 1; n.valueFloat[1] = 1; n.valueFloat[2] = 1;
            n.outputs.push_back(makePin(pid, "Value", kPinType::Vec3, true, 0, 1,1,1,1));
            break;
        case kShaderNodeType::ConstVec4:
            n.valueFloat[0] = 1; n.valueFloat[1] = 1; n.valueFloat[2] = 1; n.valueFloat[3] = 1;
            n.outputs.push_back(makePin(pid, "Value", kPinType::Vec4, true, 0, 1,1,1,1));
            break;

        // --- Built-in inputs ---
        case kShaderNodeType::UVCoord:
            n.outputs.push_back(makePin(pid, "UV", kPinType::Vec2, true));
            break;
        case kShaderNodeType::Time:
            n.outputs.push_back(makePin(pid, "Time", kPinType::Float, true));
            break;
        case kShaderNodeType::VertexColor:
            n.outputs.push_back(makePin(pid, "Color", kPinType::Vec3, true));
            break;
        case kShaderNodeType::WorldPosition:
            n.outputs.push_back(makePin(pid, "Position", kPinType::Vec3, true));
            break;
        case kShaderNodeType::ViewDirection:
            n.outputs.push_back(makePin(pid, "Direction", kPinType::Vec3, true));
            break;
        case kShaderNodeType::VertexNormal:
            n.outputs.push_back(makePin(pid, "Normal", kPinType::Vec3, true));
            break;

        // --- Textures ---
        case kShaderNodeType::Texture2D:
            n.valueStr = "textureSampler";
            n.inputs.push_back(makePin(newId(), "UV",     kPinType::Vec2,      false));
            n.outputs.push_back(makePin(pid,    "RGBA",   kPinType::Vec4,      true));
            n.outputs.push_back(makePin(newId(),"RGB",    kPinType::Vec3,      true));
            n.outputs.push_back(makePin(newId(),"Alpha",  kPinType::Float,     true));
            break;
        case kShaderNodeType::TextureCube:
            n.valueStr = "cubeSampler";
            n.inputs.push_back(makePin(newId(), "Dir",    kPinType::Vec3,      false));
            n.outputs.push_back(makePin(pid,    "RGBA",   kPinType::Vec4,      true));
            n.outputs.push_back(makePin(newId(),"RGB",    kPinType::Vec3,      true));
            break;

        // --- Arithmetic ---
        case kShaderNodeType::Add:
        case kShaderNodeType::Subtract:
        case kShaderNodeType::Multiply:
        case kShaderNodeType::Divide:
        case kShaderNodeType::Min:
        case kShaderNodeType::Max:
            n.inputs.push_back(makePin(newId(), "A",    kPinType::Vec3, false));
            n.inputs.push_back(makePin(newId(), "B",    kPinType::Vec3, false));
            n.outputs.push_back(makePin(pid,    "Out",  kPinType::Vec3, true));
            break;

        case kShaderNodeType::Dot:
            n.inputs.push_back(makePin(newId(), "A",    kPinType::Vec3,  false));
            n.inputs.push_back(makePin(newId(), "B",    kPinType::Vec3,  false));
            n.outputs.push_back(makePin(pid,    "Out",  kPinType::Float, true));
            break;

        case kShaderNodeType::Cross:
            n.inputs.push_back(makePin(newId(), "A",    kPinType::Vec3,  false));
            n.inputs.push_back(makePin(newId(), "B",    kPinType::Vec3,  false));
            n.outputs.push_back(makePin(pid,    "Out",  kPinType::Vec3,  true));
            break;

        case kShaderNodeType::Normalize:
        case kShaderNodeType::Abs:
        case kShaderNodeType::Floor:
        case kShaderNodeType::Ceil:
        case kShaderNodeType::Fract:
        case kShaderNodeType::Sqrt:
        case kShaderNodeType::OneMinus:
            n.inputs.push_back(makePin(newId(), "In",   kPinType::Vec3, false));
            n.outputs.push_back(makePin(pid,    "Out",  kPinType::Vec3, true));
            break;

        case kShaderNodeType::Length:
            n.inputs.push_back(makePin(newId(), "In",   kPinType::Vec3,  false));
            n.outputs.push_back(makePin(pid,    "Out",  kPinType::Float, true));
            break;

        case kShaderNodeType::Clamp:
            n.inputs.push_back(makePin(newId(), "Value", kPinType::Vec3,  false));
            n.inputs.push_back(makePin(newId(), "Min",   kPinType::Float, false, 0.f));
            n.inputs.push_back(makePin(newId(), "Max",   kPinType::Float, false, 1.f));
            n.outputs.push_back(makePin(pid,    "Out",   kPinType::Vec3,  true));
            break;

        case kShaderNodeType::Mix:
            n.inputs.push_back(makePin(newId(), "A",   kPinType::Vec3,  false));
            n.inputs.push_back(makePin(newId(), "B",   kPinType::Vec3,  false));
            n.inputs.push_back(makePin(newId(), "T",   kPinType::Float, false));
            n.outputs.push_back(makePin(pid,    "Out", kPinType::Vec3,  true));
            break;

        case kShaderNodeType::Pow:
            n.inputs.push_back(makePin(newId(), "Base", kPinType::Vec3,  false));
            n.inputs.push_back(makePin(newId(), "Exp",  kPinType::Float, false, 2.f));
            n.outputs.push_back(makePin(pid,    "Out",  kPinType::Vec3,  true));
            break;

        case kShaderNodeType::Step:
            n.inputs.push_back(makePin(newId(), "Edge", kPinType::Float, false));
            n.inputs.push_back(makePin(newId(), "X",    kPinType::Vec3,  false));
            n.outputs.push_back(makePin(pid,    "Out",  kPinType::Vec3,  true));
            break;

        case kShaderNodeType::Smoothstep:
            n.inputs.push_back(makePin(newId(), "Edge0", kPinType::Float, false, 0.f));
            n.inputs.push_back(makePin(newId(), "Edge1", kPinType::Float, false, 1.f));
            n.inputs.push_back(makePin(newId(), "X",     kPinType::Vec3,  false));
            n.outputs.push_back(makePin(pid,    "Out",   kPinType::Vec3,  true));
            break;

        // --- Vector ops ---
        case kShaderNodeType::Split:
            n.inputs.push_back(makePin(newId(), "In",  kPinType::Vec4,  false));
            n.outputs.push_back(makePin(pid,         "X", kPinType::Float, true));
            n.outputs.push_back(makePin(newId(),     "Y", kPinType::Float, true));
            n.outputs.push_back(makePin(newId(),     "Z", kPinType::Float, true));
            n.outputs.push_back(makePin(newId(),     "W", kPinType::Float, true));
            break;

        case kShaderNodeType::Combine:
            n.inputs.push_back(makePin(newId(), "X", kPinType::Float, false));
            n.inputs.push_back(makePin(newId(), "Y", kPinType::Float, false));
            n.inputs.push_back(makePin(newId(), "Z", kPinType::Float, false));
            n.inputs.push_back(makePin(newId(), "W", kPinType::Float, false, 1.f));
            n.outputs.push_back(makePin(pid, "Vec4", kPinType::Vec4,  true));
            n.outputs.push_back(makePin(newId(), "Vec3", kPinType::Vec3, true));
            n.outputs.push_back(makePin(newId(), "Vec2", kPinType::Vec2, true));
            break;

        case kShaderNodeType::Swizzle:
            n.valueStr = "xyz";
            n.inputs.push_back(makePin(newId(), "In",  kPinType::Vec4, false));
            n.outputs.push_back(makePin(pid,    "Out", kPinType::Vec3, true));
            break;

        // --- Material inputs ---
        case kShaderNodeType::MaterialTiling:
            n.outputs.push_back(makePin(pid, "Tiling", kPinType::Vec2, true));
            break;
        case kShaderNodeType::MaterialAmbient:
            n.outputs.push_back(makePin(pid, "Ambient", kPinType::Vec3, true));
            break;
        case kShaderNodeType::MaterialDiffuse:
            n.outputs.push_back(makePin(pid, "Diffuse", kPinType::Vec3, true));
            break;
        case kShaderNodeType::MaterialSpecular:
            n.outputs.push_back(makePin(pid, "Specular", kPinType::Vec3, true));
            break;
        case kShaderNodeType::MaterialShininess:
            n.outputs.push_back(makePin(pid, "Shininess", kPinType::Float, true));
            break;
        case kShaderNodeType::MaterialMetallic:
            n.outputs.push_back(makePin(pid, "Metallic", kPinType::Float, true));
            break;
        case kShaderNodeType::MaterialRoughness:
            n.outputs.push_back(makePin(pid, "Roughness", kPinType::Float, true));
            break;

        // --- Outputs ---
        case kShaderNodeType::OutputFlat:
            n.inputs.push_back(makePin(newId(), "Color",  kPinType::Vec3,  false, 0, 1,1,1));
            n.inputs.push_back(makePin(newId(), "Alpha",  kPinType::Float, false, 1.f));
            break;

        case kShaderNodeType::OutputPhong:
            n.inputs.push_back(makePin(newId(), "Albedo",    kPinType::Vec3,  false, 0, 1,1,1));
            n.inputs.push_back(makePin(newId(), "Alpha",     kPinType::Float, false, 1.f));
            n.inputs.push_back(makePin(newId(), "Specular",  kPinType::Vec3,  false));
            n.inputs.push_back(makePin(newId(), "Emissive",  kPinType::Vec3,  false));
            break;

        case kShaderNodeType::OutputPBR:
            n.inputs.push_back(makePin(newId(), "Albedo",    kPinType::Vec3,  false, 0, 1,1,1));
            n.inputs.push_back(makePin(newId(), "Alpha",     kPinType::Float, false, 1.f));
            n.inputs.push_back(makePin(newId(), "Metallic",  kPinType::Float, false, 0.f));
            n.inputs.push_back(makePin(newId(), "Roughness", kPinType::Float, false, 0.5f));
            n.inputs.push_back(makePin(newId(), "Normal",    kPinType::Vec3,  false));
            n.inputs.push_back(makePin(newId(), "AO",        kPinType::Float, false, 1.f));
            n.inputs.push_back(makePin(newId(), "Emissive",  kPinType::Vec3,  false));
            break;

        default: break;
    }

    return n;
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

static nlohmann::json pinToJson(const kShaderPin& p)
{
    nlohmann::json j;
    j["id"]       = p.id;
    j["name"]     = p.name;
    j["type"]     = (int)p.type;
    j["isOutput"] = p.isOutput;
    j["defFloat"] = p.defFloat;
    j["defVec"]   = { p.defVec[0], p.defVec[1], p.defVec[2], p.defVec[3] };
    return j;
}

static kShaderPin pinFromJson(const nlohmann::json& j)
{
    kShaderPin p;
    p.id       = j["id"];
    p.name     = j["name"].get<std::string>();
    p.type     = (kPinType)(int)j["type"];
    p.isOutput = j["isOutput"];
    p.defFloat = j.value("defFloat", 0.f);
    if (j.contains("defVec") && j["defVec"].is_array() && j["defVec"].size() == 4)
    {
        p.defVec[0] = j["defVec"][0];
        p.defVec[1] = j["defVec"][1];
        p.defVec[2] = j["defVec"][2];
        p.defVec[3] = j["defVec"][3];
    }
    return p;
}

nlohmann::json kShaderGraph::toJson() const
{
    nlohmann::json j;
    j["uuid"]   = uuid;
    j["name"]   = name;
    j["nextId"] = nextId;

    auto& jnodes = j["nodes"] = nlohmann::json::array();
    for (const auto& n : nodes)
    {
        nlohmann::json jn;
        jn["id"]         = n.id;
        jn["type"]       = (int)n.type;
        jn["name"]       = n.name;
        jn["x"]         = n.posX;
        jn["y"]         = n.posY;
        jn["valueFloat"] = { n.valueFloat[0], n.valueFloat[1], n.valueFloat[2], n.valueFloat[3] };
        jn["valueStr"]   = n.valueStr;
        jn["valueBool"]  = n.valueBool;
        jn["sizeX"]      = n.sizeX;
        jn["sizeY"]      = n.sizeY;
        jn["comment"]    = n.comment;

        auto& ji = jn["inputs"]  = nlohmann::json::array();
        for (const auto& p : n.inputs) ji.push_back(pinToJson(p));
        auto& jo = jn["outputs"] = nlohmann::json::array();
        for (const auto& p : n.outputs) jo.push_back(pinToJson(p));

        jnodes.push_back(jn);
    }

    auto& jlinks = j["links"] = nlohmann::json::array();
    for (const auto& l : links)
    {
        nlohmann::json jl;
        jl["id"]       = l.id;
        jl["fromNode"] = l.fromNode;
        jl["fromPin"]  = l.fromPin;
        jl["toNode"]   = l.toNode;
        jl["toPin"]    = l.toPin;
        jlinks.push_back(jl);
    }

    return j;
}

void kShaderGraph::fromJson(const nlohmann::json& j)
{
    uuid   = j.value("uuid",   "");
    name   = j.value("name",   "Untitled");
    nextId = j.value("nextId", 1);

    nodes.clear();
    links.clear();

    for (const auto& jn : j.value("nodes", nlohmann::json::array()))
    {
        kShaderNode n;
        n.id    = jn["id"];
        n.type  = (kShaderNodeType)(int)jn["type"];
        n.name  = jn.value("name", "");
        n.posX  = jn.value("x",    0.f);
        n.posY  = jn.value("y",    0.f);
        n.valueStr  = jn.value("valueStr",  "");
        n.valueBool = jn.value("valueBool", false);
        n.sizeX     = jn.value("sizeX", 300.f);
        n.sizeY     = jn.value("sizeY", 200.f);
        n.comment   = jn.value("comment", std::string());
        if (jn.contains("valueFloat") && jn["valueFloat"].size() == 4)
        {
            for (int i = 0; i < 4; ++i) n.valueFloat[i] = jn["valueFloat"][i];
        }
        for (const auto& p : jn.value("inputs",  nlohmann::json::array()))
            n.inputs.push_back(pinFromJson(p));
        for (const auto& p : jn.value("outputs", nlohmann::json::array()))
            n.outputs.push_back(pinFromJson(p));
        nodes.push_back(n);
    }

    for (const auto& jl : j.value("links", nlohmann::json::array()))
    {
        kShaderLink l;
        l.id       = jl["id"];
        l.fromNode = jl["fromNode"];
        l.fromPin  = jl["fromPin"];
        l.toNode   = jl["toNode"];
        l.toPin    = jl["toPin"];
        links.push_back(l);
    }
}

// ===========================================================================
// kShaderCompiler
// ===========================================================================

static kString defaultForType(kPinType t, const float* defVec, float defF)
{
    switch (t)
    {
        case kPinType::Float: return std::to_string(defF) + (defF == (int)defF ? "" : "");
        case kPinType::Vec2:  return "vec2(" + std::to_string(defVec[0]) + ", " + std::to_string(defVec[1]) + ")";
        case kPinType::Vec3:  return "vec3(" + std::to_string(defVec[0]) + ", " + std::to_string(defVec[1]) + ", " + std::to_string(defVec[2]) + ")";
        case kPinType::Vec4:  return "vec4(" + std::to_string(defVec[0]) + ", " + std::to_string(defVec[1]) + ", " + std::to_string(defVec[2]) + ", " + std::to_string(defVec[3]) + ")";
        default: return "0.0";
    }
}

kString kShaderCompiler::pinDefault(const kShaderPin& p)
{
    return defaultForType(p.type, p.defVec, p.defFloat);
}

kString kShaderCompiler::promote(const kString& expr, kPinType actual, kPinType target)
{
    if (actual == target) return expr;
    if (actual == kPinType::Float)
    {
        if (target == kPinType::Vec2) return "vec2(" + expr + ")";
        if (target == kPinType::Vec3) return "vec3(" + expr + ")";
        if (target == kPinType::Vec4) return "vec4(vec3(" + expr + "), 1.0)";
    }
    if (actual == kPinType::Vec3 && target == kPinType::Vec4)
        return "vec4(" + expr + ", 1.0)";
    if (actual == kPinType::Vec4 && target == kPinType::Vec3)
        return "(" + expr + ").rgb";
    return expr;
}

// Get the GLSL expression for a specific input pin, recursively emitting upstream nodes.
kString kShaderCompiler::emitPin(const kShaderGraph& g, int nodeId, int pinId, Ctx& ctx)
{
    const kShaderLink* link = g.incomingLink(nodeId, pinId);
    if (!link) {
        // No connection: use pin default
        const kShaderNode* n = g.findNode(nodeId);
        for (const auto& p : n->inputs)
            if (p.id == pinId) return pinDefault(p);
        return "0.0";
    }

    // Resolve the source through pass-through anchor nodes. Anchors carry no
    // shader semantics: a wire routed through them is simply a wire from the
    // original source node to this input.
    int fromNode = link->fromNode;
    int fromPin  = link->fromPin;

    while (true)
    {
        const kShaderNode* src = g.findNode(fromNode);
        if (!src || src->type != kShaderNodeType::Anchor)
            break;

        if (src->inputs.empty())
            break;
        const kShaderLink* prev = g.incomingLink(fromNode, src->inputs[0].id);
        if (!prev)
        {
            // Dangling anchor: fall back to the destination pin's default.
            const kShaderNode* dst = g.findNode(nodeId);
            for (const auto& p : dst->inputs)
                if (p.id == pinId) return pinDefault(p);
            return "0.0";
        }
        fromNode = prev->fromNode;
        fromPin  = prev->fromPin;
    }

    // Emit the source node and get its output var
    kString srcVar = emitNode(g, fromNode, ctx);

    // Find the actual output pin type
    const kShaderNode* src = g.findNode(fromNode);
    kPinType actualType = kPinType::Float;
    int pinIdx = 0;
    for (int i = 0; i < (int)src->outputs.size(); ++i)
        if (src->outputs[i].id == fromPin) { actualType = src->outputs[i].type; pinIdx = i; break; }

    // Find the input pin's expected type
    const kShaderNode* dst = g.findNode(nodeId);
    kPinType targetType = actualType;
    for (const auto& p : dst->inputs)
        if (p.id == pinId) { targetType = p.type; break; }

    // Multi-output nodes: variable is name-mangled with pin index
    kString expr = srcVar;
    if (src->outputs.size() > 1)
        expr = srcVar + "_out" + std::to_string(pinIdx);

    return promote(expr, actualType, targetType);
}

kString kShaderCompiler::emitNode(const kShaderGraph& g, int nodeId, Ctx& ctx)
{
    if (ctx.nodeVar.count(nodeId)) return ctx.nodeVar[nodeId];

    const kShaderNode* n = g.findNode(nodeId);
    if (!n) return "0.0";

    kString var = ctx.newVar();
    ctx.nodeVar[nodeId] = var;

    auto ep = [&](int idx) -> kString {
        if (idx >= (int)n->inputs.size()) return "0.0";
        return emitPin(g, nodeId, n->inputs[idx].id, ctx);
    };

    // Shorthand output declarations
    auto declOut = [&](kPinType t, const kString& expr) {
        ctx.body.push_back(kString(kPinTypeName(t)) + " " + var + " = " + expr + ";");
    };
    auto declOut2 = [&](kPinType t0, const kString& e0,
                        kPinType t1, const kString& e1,
                        kPinType t2, const kString& e2) {
        ctx.body.push_back(kString(kPinTypeName(t0)) + " " + var + "_out0 = " + e0 + ";");
        ctx.body.push_back(kString(kPinTypeName(t1)) + " " + var + "_out1 = " + e1 + ";");
        ctx.body.push_back(kString(kPinTypeName(t2)) + " " + var + "_out2 = " + e2 + ";");
    };

    switch (n->type)
    {
        // --- Constants ---
        case kShaderNodeType::ConstFloat:
            declOut(kPinType::Float, std::to_string(n->valueFloat[0]));
            break;
        case kShaderNodeType::ConstVec2:
            declOut(kPinType::Vec2,
                "vec2(" + std::to_string(n->valueFloat[0]) + ", " + std::to_string(n->valueFloat[1]) + ")");
            break;
        case kShaderNodeType::ConstVec3:
            declOut(kPinType::Vec3,
                "vec3(" + std::to_string(n->valueFloat[0]) + ", " + std::to_string(n->valueFloat[1]) + ", " + std::to_string(n->valueFloat[2]) + ")");
            break;
        case kShaderNodeType::ConstVec4:
            declOut(kPinType::Vec4,
                "vec4(" + std::to_string(n->valueFloat[0]) + ", " + std::to_string(n->valueFloat[1]) + ", " + std::to_string(n->valueFloat[2]) + ", " + std::to_string(n->valueFloat[3]) + ")");
            break;

        // --- Built-in inputs ---
        case kShaderNodeType::UVCoord:
            declOut(kPinType::Vec2, "v_texCoord");
            break;
        case kShaderNodeType::Time:
            if (!ctx.emittedSamplerNames.count("u_time"))
            {
                ctx.uniforms.push_back("uniform float u_time;");
                ctx.emittedSamplerNames.insert("u_time");
            }
            declOut(kPinType::Float, "u_time");
            break;
        case kShaderNodeType::VertexColor:
            declOut(kPinType::Vec3, "v_color");
            break;
        case kShaderNodeType::WorldPosition:
            declOut(kPinType::Vec3, "v_worldPos");
            break;
        case kShaderNodeType::ViewDirection:
            declOut(kPinType::Vec3, "normalize(viewPos - v_worldPos)");
            break;
        case kShaderNodeType::VertexNormal:
            declOut(kPinType::Vec3, "normalize(v_N)");
            break;

        // --- Texture sampling ---
        case kShaderNodeType::Texture2D:
        {
            kString sampName = n->valueStr.empty() ? "texSampler" : n->valueStr;
            if (!ctx.emittedSamplerNames.count(sampName))
            {
                ctx.samplers.push_back("uniform sampler2D " + sampName + ";");
                ctx.emittedSamplerNames.insert(sampName);
            }
            kString uv = ep(0);
            kString rgba = var + "_out0";
            ctx.body.push_back("vec4 " + rgba + " = texture(" + sampName + ", " + uv + ");");
            ctx.body.push_back("vec3 " + var + "_out1 = " + rgba + ".rgb;");
            ctx.body.push_back("float " + var + "_out2 = " + rgba + ".a;");
            break;
        }
        case kShaderNodeType::TextureCube:
        {
            kString sampName = n->valueStr.empty() ? "cubeSampler" : n->valueStr;
            if (!ctx.emittedSamplerNames.count(sampName))
            {
                ctx.samplers.push_back("uniform samplerCube " + sampName + ";");
                ctx.emittedSamplerNames.insert(sampName);
            }
            kString dir = ep(0);
            ctx.body.push_back("vec4 " + var + "_out0 = texture(" + sampName + ", " + dir + ");");
            ctx.body.push_back("vec3 " + var + "_out1 = " + var + "_out0.rgb;");
            break;
        }

        // --- Arithmetic ---
        case kShaderNodeType::Add:      declOut(kPinType::Vec3, ep(0) + " + "  + ep(1)); break;
        case kShaderNodeType::Subtract: declOut(kPinType::Vec3, ep(0) + " - "  + ep(1)); break;
        case kShaderNodeType::Multiply: declOut(kPinType::Vec3, ep(0) + " * "  + ep(1)); break;
        case kShaderNodeType::Divide:   declOut(kPinType::Vec3, ep(0) + " / "  + ep(1)); break;
        case kShaderNodeType::Min:      declOut(kPinType::Vec3, "min(" + ep(0) + ", " + ep(1) + ")"); break;
        case kShaderNodeType::Max:      declOut(kPinType::Vec3, "max(" + ep(0) + ", " + ep(1) + ")"); break;
        case kShaderNodeType::Dot:      declOut(kPinType::Float,"dot(" + ep(0) + ", " + ep(1) + ")"); break;
        case kShaderNodeType::Cross:    declOut(kPinType::Vec3, "cross(" + ep(0) + ", " + ep(1) + ")"); break;
        case kShaderNodeType::Normalize:declOut(kPinType::Vec3, "normalize(" + ep(0) + ")"); break;
        case kShaderNodeType::Length:   declOut(kPinType::Float,"length(" + ep(0) + ")"); break;
        case kShaderNodeType::Abs:      declOut(kPinType::Vec3, "abs(" + ep(0) + ")"); break;
        case kShaderNodeType::Floor:    declOut(kPinType::Vec3, "floor(" + ep(0) + ")"); break;
        case kShaderNodeType::Ceil:     declOut(kPinType::Vec3, "ceil(" + ep(0) + ")"); break;
        case kShaderNodeType::Fract:    declOut(kPinType::Vec3, "fract(" + ep(0) + ")"); break;
        case kShaderNodeType::Sqrt:     declOut(kPinType::Vec3, "sqrt(" + ep(0) + ")"); break;
        case kShaderNodeType::OneMinus: declOut(kPinType::Vec3, "(1.0 - " + ep(0) + ")"); break;
        case kShaderNodeType::Pow:      declOut(kPinType::Vec3, "pow(" + ep(0) + ", vec3(" + ep(1) + "))"); break;
        case kShaderNodeType::Step:     declOut(kPinType::Vec3, "step(" + ep(0) + ", " + ep(1) + ")"); break;

        case kShaderNodeType::Clamp:
            declOut(kPinType::Vec3, "clamp(" + ep(0) + ", vec3(" + ep(1) + "), vec3(" + ep(2) + "))");
            break;
        case kShaderNodeType::Mix:
            declOut(kPinType::Vec3, "mix(" + ep(0) + ", " + ep(1) + ", " + ep(2) + ")");
            break;
        case kShaderNodeType::Smoothstep:
            declOut(kPinType::Vec3, "smoothstep(" + ep(0) + ", " + ep(1) + ", " + ep(2) + ")");
            break;

        // --- Vector ops ---
        case kShaderNodeType::Split:
        {
            kString src = ep(0);
            ctx.body.push_back("float " + var + "_out0 = (" + src + ").x;");
            ctx.body.push_back("float " + var + "_out1 = (" + src + ").y;");
            ctx.body.push_back("float " + var + "_out2 = (" + src + ").z;");
            ctx.body.push_back("float " + var + "_out3 = (" + src + ").w;");
            break;
        }
        case kShaderNodeType::Combine:
        {
            kString x = ep(0), y = ep(1), z = ep(2), w = ep(3);
            ctx.body.push_back("vec4 " + var + "_out0 = vec4(" + x + ", " + y + ", " + z + ", " + w + ");");
            ctx.body.push_back("vec3 " + var + "_out1 = vec3(" + x + ", " + y + ", " + z + ");");
            ctx.body.push_back("vec2 " + var + "_out2 = vec2(" + x + ", " + y + ");");
            break;
        }
        case kShaderNodeType::Swizzle:
        {
            kString mask = n->valueStr.empty() ? "xyz" : n->valueStr;
            kString srcE = ep(0);
            kPinType outT = (mask.size() == 1) ? kPinType::Float :
                            (mask.size() == 2) ? kPinType::Vec2 :
                            (mask.size() == 3) ? kPinType::Vec3 : kPinType::Vec4;
            declOut(outT, "(" + srcE + ")." + mask);
            break;
        }

        // --- Material inputs ---
        case kShaderNodeType::MaterialTiling:     declOut(kPinType::Vec2,  "material.tiling");    break;
        case kShaderNodeType::MaterialAmbient:    declOut(kPinType::Vec3,  "material.ambient");   break;
        case kShaderNodeType::MaterialDiffuse:    declOut(kPinType::Vec3,  "material.diffuse");   break;
        case kShaderNodeType::MaterialSpecular:   declOut(kPinType::Vec3,  "material.specular");  break;
        case kShaderNodeType::MaterialShininess:  declOut(kPinType::Float, "material.shininess"); break;
        case kShaderNodeType::MaterialMetallic:   declOut(kPinType::Float, "material.metallic");  break;
        case kShaderNodeType::MaterialRoughness:  declOut(kPinType::Float, "material.roughness"); break;

        default:
            declOut(kPinType::Vec3, "vec3(1.0)");
            break;
    }

    return var;
}

// ---------------------------------------------------------------------------
// Vertex template (shared for all output types, with bone skinning)
// ---------------------------------------------------------------------------
kString kShaderCompiler::vertexTemplate()
{
    return R"(#version 330 core

const int MAX_BONES          = 128;
const int MAX_BONE_INFLUENCE = 4;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform mat4 normalMatrix;
uniform mat4 finalBonesMatrices[MAX_BONES];

layout(location = 0) in vec3  vertexPosition;
layout(location = 1) in vec3  vertexColor;
layout(location = 2) in vec2  vertexTexCoord;
layout(location = 3) in vec3  vertexNormal;
layout(location = 4) in vec3  vertexTangent;
layout(location = 5) in vec3  vertexBitangent;
layout(location = 6) in ivec4 boneIDs;
layout(location = 7) in vec4  weights;

out vec3 v_worldPos;
out vec3 v_color;
out vec2 v_texCoord;
out vec3 v_T;
out vec3 v_B;
out vec3 v_N;

void main()
{
    vec4  totalPos       = vec4(0.0);
    vec3  totalNormal    = vec3(0.0);
    vec3  totalTangent   = vec3(0.0);
    vec3  totalBitangent = vec3(0.0);
    float totalWeight    = 0.0;

    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int   boneID = boneIDs[i];
        float weight = weights[i];
        if (boneID < 0 || weight <= 0.0) continue;
        if (boneID >= MAX_BONES)
        {
            totalPos       = vec4(0.0);
            totalNormal    = vec3(0.0);
            totalTangent   = vec3(0.0);
            totalBitangent = vec3(0.0);
            break;
        }
        totalPos       += finalBonesMatrices[boneID] * vec4(vertexPosition, 1.0) * weight;
        mat3 nm         = transpose(inverse(mat3(finalBonesMatrices[boneID])));
        totalNormal    += nm * vertexNormal    * weight;
        totalTangent   += nm * vertexTangent   * weight;
        totalBitangent += nm * vertexBitangent * weight;
        totalWeight    += weight;
    }
    if (totalWeight == 0.0)
    {
        totalPos       = vec4(vertexPosition, 1.0);
        totalNormal    = vertexNormal;
        totalTangent   = vertexTangent;
        totalBitangent = vertexBitangent;
    }

    bool anim = totalWeight > 0.0;
    vec3 useN = anim ? totalNormal    : vertexNormal;
    vec3 useT = anim ? totalTangent   : vertexTangent;
    vec3 useB = anim ? totalBitangent : vertexBitangent;

    mat3 nm3      = mat3(normalMatrix);
    vec3 worldPos = vec3(modelMatrix * totalPos);

    v_worldPos      = worldPos;
    v_color         = vertexColor;
    v_texCoord      = vertexTexCoord;
    v_T             = nm3 * useT;
    v_B             = nm3 * useB;
    v_N             = nm3 * useN;
    gl_Position     = projectionMatrix * viewMatrix * vec4(worldPos, 1.0);
}
)";
}

// ---------------------------------------------------------------------------
// Fragment preamble (material + light structs + standard uniforms)
// ---------------------------------------------------------------------------
kString kShaderCompiler::fragmentPreamble(const kShaderGraph& g, const kShaderNode& outNode,
                                           const Ctx& ctx)
{
    bool needsMat    = true;  // always include material
    bool needsLights = (outNode.type == kShaderNodeType::OutputPhong ||
                        outNode.type == kShaderNodeType::OutputPBR);

    kString s = "#version 330 core\n\n";

    s += "struct Material {\n"
         "    vec2  tiling;\n"
         "    vec3  ambient;\n"
         "    vec3  diffuse;\n"
         "    vec3  specular;\n"
         "    float shininess;\n"
         "    float metallic;\n"
         "    float roughness;\n"
         "};\n\n";

    if (needsLights)
    {
        s += "struct SunLight {\n"
             "    float power;\n"
             "    vec3  direction;\n"
             "    vec3  diffuse;\n"
             "    vec3  specular;\n"
             "};\n\n"
             "struct PointLight {\n"
             "    float power;\n"
             "    vec3  position;\n"
             "    float constant;\n"
             "    float linear;\n"
             "    float quadratic;\n"
             "    vec3  diffuse;\n"
             "    vec3  specular;\n"
             "};\n\n"
             "struct SpotLight {\n"
             "    float power;\n"
             "    vec3  position;\n"
             "    vec3  direction;\n"
             "    float cutOff;\n"
             "    float outerCutOff;\n"
             "    float constant;\n"
             "    float linear;\n"
             "    float quadratic;\n"
             "    vec3  diffuse;\n"
             "    vec3  specular;\n"
             "};\n\n";

        s += "uniform int        sunLightNum;\n"
             "uniform SunLight   sunLights[32];\n"
             "uniform int        pointLightNum;\n"
             "uniform PointLight pointLights[32];\n"
             "uniform int        spotLightNum;\n"
             "uniform SpotLight  spotLights[32];\n\n";

        s += "uniform vec3        sceneAmbient;\n"
             "uniform samplerCube skyboxMap;\n"
             "uniform bool        skyboxAmbientEnabled;\n"
             "uniform float       skyboxAmbientStrength;\n\n";

        // Cascaded shadow maps (matches the renderer's CSM uniforms).
        s += "uniform mat4           viewMatrix;\n"
             "uniform sampler2DArray shadowMapArray;\n"
             "uniform mat4           lightSpaceMatrices[4];\n"
             "uniform vec4           cascadeSplits;\n"
             "uniform int            cascadeCount;\n"
             "uniform float          shadowResolution;\n"
             "uniform bool           enableShadow;\n"
             "uniform bool           receiveShadow;\n"
             "uniform float          shadowBias;\n"
             "uniform float          shadowNormalBias;\n"
             "uniform float          shadowNormalOffset;\n"
             "uniform float          shadowSoftness;\n\n";
    }

    s += "uniform Material  material;\n"
         "uniform vec3      viewPos;\n\n";

    // Custom sampler uniforms from emitted nodes
    for (const auto& samp : ctx.samplers)
        s += samp + "\n";
    // Other uniforms
    for (const auto& u : ctx.uniforms)
        s += u + "\n";
    if (!ctx.samplers.empty() || !ctx.uniforms.empty())
        s += "\n";

    s += "in vec3 v_worldPos;\n"
         "in vec3 v_color;\n"
         "in vec2 v_texCoord;\n"
         "in vec3 v_T;\n"
         "in vec3 v_B;\n"
         "in vec3 v_N;\n\n"
         "out vec4 fragColor;\n\n";

    return s;
}

// ---------------------------------------------------------------------------
// Lighting function bodies
// ---------------------------------------------------------------------------
kString kShaderCompiler::lightingCode(kShaderNodeType outType)
{
    if (outType == kShaderNodeType::OutputFlat) return "";

    if (outType == kShaderNodeType::OutputPhong)
    {
        return R"(
float csmSplit(int i){ if(i==0)return cascadeSplits.x; if(i==1)return cascadeSplits.y; if(i==2)return cascadeSplits.z; return cascadeSplits.w; }

// World-space size of one shadow-map texel for a cascade (light ortho [0][0]==1/radius).
float csmTexelWorld(int l){ float m = abs(lightSpaceMatrices[l][0][0]); return (m > 0.0) ? 2.0 / (m * max(shadowResolution, 1.0)) : 0.0; }

// Constant + slope-scaled receiver bias (clamped tan(acos(N·L))).
float csmBias(vec3 n, vec3 l){ float ndl = max(dot(normalize(n), normalize(l)), 0.0); return shadowBias + shadowNormalBias * min(tan(acos(max(ndl, 1e-3))), 8.0); }

bool csmProject(int l, vec3 wp, out vec3 p){ vec4 ls = lightSpaceMatrices[l] * vec4(wp, 1.0); p = ls.xyz / ls.w; p = p * 0.5 + 0.5; return p.z <= 1.0 && p.x >= 0.0 && p.x <= 1.0 && p.y >= 0.0 && p.y <= 1.0; }

float csmPCF(int l, vec3 p, float b){
    vec2 ts = (1.0 / vec2(textureSize(shadowMapArray, 0).xy)) * max(shadowSoftness, 0.5); float s = 0.0;
    for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++)
        s += (p.z - b > texture(shadowMapArray, vec3(p.xy + vec2(x, y) * ts, float(l))).r) ? 1.0 : 0.0;
    return s / 9.0;
}

float csmShadow(vec3 wp, vec3 n, vec3 lightDir)
{
    if (!enableShadow || !receiveShadow || cascadeCount <= 0) return 0.0;
    vec3 l = normalize(-lightDir);
    float fd = abs((viewMatrix * vec4(wp, 1.0)).z);
    int layer = cascadeCount - 1;
    for (int i = 0; i < cascadeCount; i++) if (fd < csmSplit(i)) { layer = i; break; }
    // Normal-offset, then constant + slope-scaled depth bias.
    vec3 sp = wp + normalize(n) * (csmTexelWorld(layer) * shadowNormalOffset);
    float b = csmBias(n, l);
    vec3 p; if (!csmProject(layer, sp, p)) return 0.0;
    float sh = csmPCF(layer, p, b);
    float sf = csmSplit(layer); float band = sf * 0.1;
    if (layer + 1 < cascadeCount && fd > sf - band)
    {
        vec3 p2;
        if (csmProject(layer + 1, sp, p2))
        {
            float t = clamp((fd - (sf - band)) / max(band, 1e-4), 0.0, 1.0);
            sh = mix(sh, csmPCF(layer + 1, p2, b), t);
        }
    }
    return sh;
}

vec3 calcSunLight(SunLight light, vec3 norm, vec3 vdir, vec3 albedo, vec3 specCol)
{
    vec3  ldir  = normalize(-light.direction);
    float diff  = max(dot(norm, ldir), 0.0);
    float shine = max(material.shininess, 1.0);
    float spec  = pow(max(dot(vdir, reflect(-ldir, norm)), 0.001), shine);
    return (light.diffuse * albedo * diff + light.specular * specCol * spec) * light.power;
}

vec3 calcPointLight(PointLight light, vec3 norm, vec3 fragPos, vec3 vdir, vec3 albedo, vec3 specCol)
{
    vec3  ldir  = normalize(light.position - fragPos);
    float diff  = max(dot(norm, ldir), 0.0);
    float shine = max(material.shininess, 1.0);
    float spec  = pow(max(dot(vdir, reflect(-ldir, norm)), 0.001), shine);
    float dist  = length(light.position - fragPos);
    float att   = light.power / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    return (light.diffuse * albedo * diff + light.specular * specCol * spec) * att;
}

vec3 calcSpotLight(SpotLight light, vec3 norm, vec3 fragPos, vec3 vdir, vec3 albedo, vec3 specCol)
{
    vec3  ldir   = normalize(light.position - fragPos);
    float diff   = max(dot(norm, ldir), 0.0);
    float shine  = max(material.shininess, 1.0);
    float spec   = pow(max(dot(vdir, reflect(-ldir, norm)), 0.001), shine);
    float theta  = dot(ldir, normalize(-light.direction));
    float eps    = light.cutOff - light.outerCutOff;
    float intens = clamp((theta - light.outerCutOff) / eps, 0.0, 1.0);
    float dist   = length(light.position - fragPos);
    float att    = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    return (light.diffuse * albedo * diff + light.specular * specCol * spec) * light.power * intens * att;
}

// Fresnel with a roughness-aware ceiling (see mesh_phong.glsl).
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
           pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Analytic split-sum specular BRDF approximation (Karis, UE4).
vec3 envBRDFApprox(vec3 specularColor, float roughness, float NoV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4  r    = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2  AB   = vec2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * AB.x + AB.y;
}
)";
    }

    // PBR
    return R"(
float csmSplit(int i){ if(i==0)return cascadeSplits.x; if(i==1)return cascadeSplits.y; if(i==2)return cascadeSplits.z; return cascadeSplits.w; }

// World-space size of one shadow-map texel for a cascade (light ortho [0][0]==1/radius).
float csmTexelWorld(int l){ float m = abs(lightSpaceMatrices[l][0][0]); return (m > 0.0) ? 2.0 / (m * max(shadowResolution, 1.0)) : 0.0; }

// Constant + slope-scaled receiver bias (clamped tan(acos(N·L))).
float csmBias(vec3 n, vec3 l){ float ndl = max(dot(normalize(n), normalize(l)), 0.0); return shadowBias + shadowNormalBias * min(tan(acos(max(ndl, 1e-3))), 8.0); }

bool csmProject(int l, vec3 wp, out vec3 p){ vec4 ls = lightSpaceMatrices[l] * vec4(wp, 1.0); p = ls.xyz / ls.w; p = p * 0.5 + 0.5; return p.z <= 1.0 && p.x >= 0.0 && p.x <= 1.0 && p.y >= 0.0 && p.y <= 1.0; }

float csmPCF(int l, vec3 p, float b){
    vec2 ts = (1.0 / vec2(textureSize(shadowMapArray, 0).xy)) * max(shadowSoftness, 0.5); float s = 0.0;
    for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++)
        s += (p.z - b > texture(shadowMapArray, vec3(p.xy + vec2(x, y) * ts, float(l))).r) ? 1.0 : 0.0;
    return s / 9.0;
}

float csmShadow(vec3 wp, vec3 n, vec3 lightDir)
{
    if (!enableShadow || !receiveShadow || cascadeCount <= 0) return 0.0;
    vec3 l = normalize(-lightDir);
    float fd = abs((viewMatrix * vec4(wp, 1.0)).z);
    int layer = cascadeCount - 1;
    for (int i = 0; i < cascadeCount; i++) if (fd < csmSplit(i)) { layer = i; break; }
    // Normal-offset, then constant + slope-scaled depth bias.
    vec3 sp = wp + normalize(n) * (csmTexelWorld(layer) * shadowNormalOffset);
    float b = csmBias(n, l);
    vec3 p; if (!csmProject(layer, sp, p)) return 0.0;
    float sh = csmPCF(layer, p, b);
    float sf = csmSplit(layer); float band = sf * 0.1;
    if (layer + 1 < cascadeCount && fd > sf - band)
    {
        vec3 p2;
        if (csmProject(layer + 1, sp, p2))
        {
            float t = clamp((fd - (sf - band)) / max(band, 1e-4), 0.0, 1.0);
            sh = mix(sh, csmPCF(layer + 1, p2, b), t);
        }
    }
    return sh;
}

const float PI = 3.14159265359;

float distGGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geoSchlick(float ndotv, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / (ndotv * (1.0 - k) + k);
}

float geoSmith(float NdotV, float NdotL, float roughness)
{
    return geoSchlick(NdotV, roughness) * geoSchlick(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 calcPBR(vec3 albedo, float metallic, float roughness, vec3 F0,
             vec3 n, vec3 v, vec3 l, vec3 radiance)
{
    vec3  h     = normalize(v + l);
    float NdotH = max(dot(n, h),   0.0);
    float NdotV = max(dot(n, v),   0.0);
    float NdotL = max(dot(n, l),   0.0);
    float NDF   = distGGX(NdotH, roughness);
    float G     = geoSmith(NdotV, NdotL, roughness);
    vec3  F     = fresnelSchlick(max(dot(h, v), 0.0), F0);
    vec3  kD    = (1.0 - F) * (1.0 - metallic);
    vec3  spec  = NDF * G * F / (4.0 * NdotV * NdotL + 0.0001);
    return (kD * albedo / PI + spec) * radiance * NdotL;
}

// Fresnel with a roughness-aware ceiling (see mesh_pbr.glsl).
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
           pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Analytic split-sum specular BRDF approximation (Karis, UE4).
vec3 envBRDFApprox(vec3 specularColor, float roughness, float NoV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4  r    = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2  AB   = vec2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * AB.x + AB.y;
}
)";
}

// ---------------------------------------------------------------------------
// Main compile entry point
// ---------------------------------------------------------------------------
kShaderCompileResult kShaderCompiler::compile(const kShaderGraph& graph)
{
    kShaderCompileResult result;

    // Find output node
    const kShaderNode* outNode = nullptr;
    for (const auto& n : graph.nodes)
        if (n.type == kShaderNodeType::OutputFlat ||
            n.type == kShaderNodeType::OutputPhong ||
            n.type == kShaderNodeType::OutputPBR)
        { outNode = &n; break; }

    if (!outNode)
    {
        result.error = "No Output node found. Add an Output (Flat / Phong / PBR) node.";
        return result;
    }

    Ctx ctx;

    // Emit each input pin of the output node
    std::vector<kString> outPinVars;
    for (const auto& pin : outNode->inputs)
        outPinVars.push_back(emitPin(graph, outNode->id, pin.id, ctx));

    // Detect features
    result.needsMaterial = true;
    result.needsLights   = (outNode->type != kShaderNodeType::OutputFlat);
    for (const auto& s : ctx.samplers)
    {
        if (s.find("albedoMap")            != kString::npos) result.usesAlbedoMap = true;
        if (s.find("normalMap")            != kString::npos) result.usesNormalMap = true;
        if (s.find("metallicRoughnessMap") != kString::npos) result.usesMetallicRoughnessMap = true;
        if (s.find("aoMap")                != kString::npos) result.usesAoMap = true;
        if (s.find("emissiveMap")          != kString::npos) result.usesEmissiveMap = true;
        else
        {
            // Collect custom sampler name
            // Format: "uniform sampler2D name;"  or  "uniform samplerCube name;"
            kString type = s.find("samplerCube") != kString::npos ? "Cube" : "2D";
            size_t nameStart = s.rfind(' ') + 1;
            size_t nameEnd   = s.rfind(';');
            if (nameStart < nameEnd)
                result.customSamplers.push_back({ s.substr(nameStart, nameEnd - nameStart), type });
        }
    }

    // Build fragment shader
    kString frag = fragmentPreamble(graph, *outNode, ctx);
    frag += lightingCode(outNode->type);
    frag += "\nvoid main()\n{\n";

    for (const auto& stmt : ctx.body)
        frag += "    " + stmt + "\n";

    // Final output assembly
    if (outNode->type == kShaderNodeType::OutputFlat)
    {
        kString color = outPinVars.size() > 0 ? outPinVars[0] : "vec3(1.0)";
        kString alpha = outPinVars.size() > 1 ? outPinVars[1] : "1.0";
        frag += "    fragColor = vec4(" + color + ", " + alpha + ");\n";
    }
    else if (outNode->type == kShaderNodeType::OutputPhong)
    {
        kString albedo   = outPinVars.size() > 0 ? outPinVars[0] : "material.diffuse";
        kString alpha    = outPinVars.size() > 1 ? outPinVars[1] : "1.0";
        kString specular = outPinVars.size() > 2 ? outPinVars[2] : "material.specular";
        kString emissive = outPinVars.size() > 3 ? outPinVars[3] : "vec3(0.0)";

        frag += R"(
    vec3 Tn   = normalize(v_T);
    vec3 Bn   = normalize(v_B);
    vec3 Nn   = normalize(v_N);
    vec3 norm = Nn;
    vec3 vdir = normalize(viewPos - v_worldPos);
    vec3 result = sceneAmbient * material.ambient;
    if (skyboxAmbientEnabled)
    {
        // Material-aware skybox ambient — the specular colour sets the
        // dielectric reflectivity, the metallic factor tints the reflection
        // toward the base colour and suppresses the diffuse term, and the
        // Phong shininess drives the reflection blur (see mesh_phong.glsl).
        float _rough = clamp(1.0 - material.shininess / (material.shininess + 1.0), 0.04, 1.0);
        float _NdotV = max(dot(norm, vdir), 0.0);
        vec3  _R     = reflect(-vdir, norm);
        vec3  _irr   = textureLod(skyboxMap, norm, 8.0).rgb;
        vec3  _pre   = textureLod(skyboxMap, _R, _rough * 6.0).rgb;
        // Fresnel F0: 4% dielectric, blended toward the base colour for metals.
        vec3  _F0    = mix(vec3(0.04), material.diffuse, material.metallic);
        vec3  _Famb  = fresnelSchlickRoughness(_NdotV, _F0, _rough);
        vec3  _kD    = (vec3(1.0) - _Famb) * (1.0 - material.metallic);
        vec3  _tint  = mix(material.specular, material.diffuse, material.metallic);
        result += _kD * material.diffuse * _irr * skyboxAmbientStrength * material.ambient
                + _pre * _tint * envBRDFApprox(vec3(1.0), _rough, _NdotV) * skyboxAmbientStrength;
    }
)";
        frag += "    vec3 _albedo   = " + albedo   + ";\n";
        frag += "    vec3 _specular = " + specular + ";\n";
        frag += R"(
    vec3 _sunDir0 = sunLightNum > 0 ? sunLights[0].direction : vec3(0.0, -1.0, 0.0);
    float _shadow = csmShadow(v_worldPos, norm, _sunDir0);
    for (int i = 0; i < sunLightNum;   i++) result += calcSunLight  (sunLights[i],   norm, vdir, _albedo, _specular) * (1.0 - _shadow);
    for (int i = 0; i < pointLightNum; i++) result += calcPointLight(pointLights[i], norm, v_worldPos, vdir, _albedo, _specular);
    for (int i = 0; i < spotLightNum;  i++) result += calcSpotLight (spotLights[i],  norm, v_worldPos, vdir, _albedo, _specular);
)";
        frag += "    result += " + emissive + ";\n";
        frag += "    fragColor = vec4(clamp(result, 0.0, 1.0), " + alpha + ");\n";
    }
    else // PBR
    {
        kString albedo    = outPinVars.size() > 0 ? outPinVars[0] : "material.diffuse";
        kString alpha     = outPinVars.size() > 1 ? outPinVars[1] : "1.0";
        kString metallic  = outPinVars.size() > 2 ? outPinVars[2] : "material.metallic";
        kString roughness = outPinVars.size() > 3 ? outPinVars[3] : "material.roughness";
        kString normal    = outPinVars.size() > 4 ? outPinVars[4] : "vec3(0.0)";
        kString ao        = outPinVars.size() > 5 ? outPinVars[5] : "1.0";
        kString emissive  = outPinVars.size() > 6 ? outPinVars[6] : "vec3(0.0)";

        frag += "    float _metallic  = " + metallic  + ";\n";
        frag += "    float _roughness = clamp(" + roughness + ", 0.04, 1.0);\n";
        frag += "    vec3  _albedo    = " + albedo + ";\n";
        frag += "    float _ao        = " + ao + ";\n";
        frag += "    vec3  _emissive  = " + emissive + ";\n";
        frag += "    vec3 Tn   = normalize(v_T);\n"
            "    vec3 Bn   = normalize(v_B);\n"
            "    vec3 Nn   = normalize(v_N);\n"
            "    vec3 norm = Nn;\n";

    // Apply normal map via TBN when the Normal input pin is connected.
    // Assumes the connected value is a tangent-space normal in [0,1] range
    // (e.g. sampled from a normal map texture).
    if ((int)outNode->inputs.size() > 4 &&
        graph.incomingLink(outNode->id, outNode->inputs[4].id))
    {
        frag += "    vec3 _nSample = normalize(" + normal + " * 2.0 - 1.0);\n";
        frag += "    norm = normalize(mat3(Tn, Bn, Nn) * _nSample);\n";
    }

    frag += R"(
    vec3 v   = normalize(viewPos - v_worldPos);
    vec3 F0  = mix(vec3(0.04), _albedo, _metallic);
    vec3 result = vec3(0.0);
    vec3 _sunDir0 = sunLightNum > 0 ? sunLights[0].direction : vec3(0.0, -1.0, 0.0);
    float _shadow = csmShadow(v_worldPos, norm, _sunDir0);
    for (int i = 0; i < sunLightNum; i++)
    {
        vec3 l        = normalize(-sunLights[i].direction);
        vec3 radiance = sunLights[i].diffuse * sunLights[i].power;
        result += calcPBR(_albedo, _metallic, _roughness, F0, norm, v, l, radiance) * (1.0 - _shadow);
    }
    for (int i = 0; i < pointLightNum; i++)
    {
        vec3  l    = normalize(pointLights[i].position - v_worldPos);
        float dist = length(pointLights[i].position - v_worldPos);
        float att  = pointLights[i].power /
                     (pointLights[i].constant + pointLights[i].linear * dist + pointLights[i].quadratic * dist * dist);
        result += calcPBR(_albedo, _metallic, _roughness, F0, norm, v, l, pointLights[i].diffuse * att);
    }
    for (int i = 0; i < spotLightNum; i++)
    {
        vec3  l      = normalize(spotLights[i].position - v_worldPos);
        float theta  = dot(l, normalize(-spotLights[i].direction));
        float eps    = spotLights[i].cutOff - spotLights[i].outerCutOff;
        float intens = clamp((theta - spotLights[i].outerCutOff) / eps, 0.0, 1.0);
        float dist   = length(spotLights[i].position - v_worldPos);
        float att    = spotLights[i].power * intens /
                       (spotLights[i].constant + spotLights[i].linear * dist + spotLights[i].quadratic * dist * dist);
        result += calcPBR(_albedo, _metallic, _roughness, F0, norm, v, l, spotLights[i].diffuse * att);
    }
    vec3 ambient = sceneAmbient * material.ambient * _albedo;
    if (skyboxAmbientEnabled)
    {
        // Physically-based skybox ambient (split-sum IBL, see mesh_pbr.glsl).
        float _NdotV = max(dot(norm, v), 0.0);
        vec3  _R     = reflect(-v, norm);
        vec3  _irr   = textureLod(skyboxMap, norm, 8.0).rgb;
        vec3  _pre   = textureLod(skyboxMap, _R, _roughness * 6.0).rgb;
        vec3  _Famb  = fresnelSchlickRoughness(_NdotV, F0, _roughness);
        vec3  _kD    = (1.0 - _Famb) * (1.0 - _metallic);
        ambient += (_kD * _albedo * _irr + _pre * envBRDFApprox(F0, _roughness, _NdotV))
                   * skyboxAmbientStrength * material.ambient;
    }
)";
        frag += "    result = ambient * _ao + result + _emissive;\n";
        frag += "    fragColor = vec4(result, " + alpha + ");\n";
    }

    frag += "}\n";

    // Assemble combined shader
    result.glsl    = "// --- VERTEX ---\n" + vertexTemplate() + "\n// --- FRAGMENT ---\n" + frag;
    result.success = true;
    return result;
}

} // namespace kemena
