///
/// Code Generator
///
/// The code generator is responsible for converting the intermediate representation outputted by *compiler.hpp/cpp*
/// (stored as a vector of pseudo-instructions) into the final bytecode (aka SCM/SCC/CS instructions).
///
#pragma once
#include "stdinc.h"
#include "compiler.hpp"
#include "program.hpp"

struct CodeGeneratorData;

/// Generates bytecode from the intermediate representation T.
///
/// This function should be overloaded/specialized for each pseudo-object generated by *compiler.hpp/cpp*.
/// By default it calls `T.generate_code(codegen)`.
///
template<typename T, typename TCodeGen>
void generate_code(const T&, TCodeGen&);
void generate_code(const CompiledData& data, CodeGenerator& codegen);
void generate_code(const CompiledScmHeader& data, CodeGeneratorData& codegen);

///
/// This function should be overloaded/specialized for each pseudo-object generated by *compiler.hpp/cpp*.
/// By default it calls `T.compiled_size(codegen)`.
///
template<typename T, typename TCodeGen>
size_t compiled_size(const T&, const TCodeGen&);
size_t compiled_size(const ArgVariant&, const CodeGenerator&);
size_t compiled_size(const CompiledData&, const CodeGenerator&);


/// Base for code generators, containing the byte emplacing code.
struct CodeGeneratorBase
{
public:
    ProgramContext& program;

    /// \returns the buffer with the generated code.
    /// \warning should be called only, and ONLY, after the code generation happened.
    const void* buffer() const
    {
        return this->bytecode.get();
    }

    /// \returns the size of the buffer with the generated code.
    /// \warning should be called only, and ONLY, after the code generation happened.
    size_t buffer_size() const
    {
        return this->max_offset;
    }

    size_t current_offset() const
    {
        return this->offset;
    }

    void emplace_u8(uint8_t value)
    {
        assert(this->offset + 1 <= max_offset);
        bytecode[this->offset++] = reinterpret_cast<uint8_t&>(value);
    }

    void emplace_u16(uint16_t value)
    {
        // TODO maybe optimize, write a entire i16 at a time? is that portable?
        //assert(this->offset + 2 <= max_offset);
        emplace_u8((value & 0x00FF) >> 0);
        emplace_u8((value & 0xFF00) >> 8);
    }

    void emplace_u32(uint32_t value)
    {
        // TODO maybe optimize, write a entire i32 at a time? is that portable?
        //assert(this->offset + 4 <= max_offset);
        emplace_u8((value & 0x000000FF) >> 0);
        emplace_u8((value & 0x0000FF00) >> 8);
        emplace_u8((value & 0x00FF0000) >> 16);
        emplace_u8((value & 0xFF000000) >> 24);
    }

    void emplace_i8(int8_t value)
    {
        return emplace_u8(reinterpret_cast<uint8_t&>(value));
    }

    void emplace_i16(int16_t value)
    {
        return emplace_u16(reinterpret_cast<uint16_t&>(value));
    }

    void emplace_i32(int32_t value)
    {
        return emplace_u32(reinterpret_cast<uint32_t&>(value));
    }

    void emplace_chars(size_t count, const char* data)
    {
        assert(this->offset + count <= max_offset);
        std::strncpy(reinterpret_cast<char*>(&this->bytecode[offset]), data, count);
        this->offset += count;
    }

    void emplace_bytes(size_t count, const void* bytes)
    {
        assert(this->offset + count <= max_offset);
        std::memcpy(&this->bytecode[offset], bytes, count);
        this->offset += count;
    }

    void emplace_fill(size_t count, uint8_t val)
    {
        assert(this->offset + count <= max_offset);
        std::memset(&this->bytecode[offset], val, count);
        this->offset += count;
    }

private:
    std::unique_ptr<uint8_t[]>  bytecode; // size == max_offset
    size_t                      offset;
    size_t                      max_offset;

protected:
    /// Cannot be instantiated, only derived from.
    CodeGeneratorBase(ProgramContext& program) : program(program)
    {}

    /// Before any of the emplacers are called (i.e. by generate_code(...)),
    /// a buffer must be allocated with this function.
    void setup_buffer(size_t max_offset)
    {
        this->offset = 0;
        this->max_offset = max_offset;
        this->bytecode.reset(new uint8_t[max_offset]);
    }
};


/// Converts intermediate representation (given by `CompilerContext`) into SCM bytecode.
struct CodeGenerator : public CodeGeneratorBase
{
    const shared_ptr<const Script>  script;
    std::vector<CompiledData>       compiled;
    const SymTable&                 symbols;

    CodeGenerator(shared_ptr<const Script> script_, std::vector<CompiledData> compiled, const SymTable& symbols, ProgramContext& program) :
        CodeGeneratorBase(program), script(std::move(script_)), compiled(std::move(compiled)), symbols(symbols)
    {
    }

    CodeGenerator(CompilerContext context, ProgramContext& program) : // consumes the context (faster)
        CodeGenerator(std::move(context.script), std::move(context.compiled), context.symbols, program)
    {}

    /// Finds the `Label::local_offsets` for all labels that are inside this script.
    ///
    /// \returns the size of this script.
    ///
    /// \warning This method is not thread-safe because it modifies states! It modifies label objects which may be
    /// in use by other code generation units.
    ///
    uint32_t compute_labels() const
    {
        uint32_t offset = 0;
        for(auto& op : this->compiled)
        {
            if(is<CompiledLabelDef>(op.data))
            {
                get<CompiledLabelDef>(op.data).label->local_offset = offset;
            }
            else
            {
                offset += compiled_size(op, *this);
            }
        }
        return offset;
    }

    void generate()
    {
        this->setup_buffer(this->script->size.value());

        for(auto& op : this->compiled)
        {
            generate_code(op, *this);
        }
    }
};

/// Converts intermediate of pure-data things (such as the SCM header) into a bytecode.
struct CodeGeneratorData : public CodeGeneratorBase
{
    CompiledScmHeader compiled;

    CodeGeneratorData(CompiledScmHeader compiled, ProgramContext& program) :
        CodeGeneratorBase(program), compiled(std::move(compiled))
    {
    }

    void generate()
    {
        this->setup_buffer(compiled.compiled_size());
        generate_code(this->compiled, *this);
    }
};

/********/

template<typename T>
inline size_t compiled_size(const T& x, const CodeGenerator&)
{
    return x.compiled_size();
}

inline size_t compiled_size(const EOAL&, const CodeGenerator&)
{
    return 1;
}

inline size_t compiled_size(const int8_t&, const CodeGenerator&)
{
    return 1 + sizeof(int8_t);
}

inline size_t compiled_size(const int16_t&, const CodeGenerator&)
{
    return 1 + sizeof(int16_t);
}

inline size_t compiled_size(const int32_t&, const CodeGenerator&)
{
    return 1 + sizeof(int32_t);
}

inline size_t compiled_size(const float& f, const CodeGenerator& codegen)
{
    if(codegen.program.opt.optimize_zero_floats && f == 0.0f)
        return 1 + sizeof(int8_t);
    if(codegen.program.opt.use_half_float)
        return 1 + sizeof(int16_t);
    return 1 + sizeof(float);
}

inline size_t compiled_size(const shared_ptr<Label>&, const CodeGenerator&)
{
    return 1 + sizeof(int32_t);
}

inline size_t compiled_size(const CompiledVar& v, const CodeGenerator&)
{
    if(v.index == nullopt || is<int32_t>(*v.index))
        return 1 + sizeof(uint16_t);
    else
        return 1 + sizeof(uint16_t) * 2 + sizeof(uint8_t) * 2;
}

inline size_t compiled_size(const CompiledString& s, const CodeGenerator& codegen)
{
    switch(s.type)
    {
        case CompiledString::Type::TextLabel8:
            return (codegen.program.opt.has_text_label_prefix? 1 : 0) + 8;
        case CompiledString::Type::TextLabel16:
            return 1 + 16;
        case CompiledString::Type::StringVar:
            return 1 + 1 + s.storage.size();
        case CompiledString::Type::String128:
            return 128;
        default:
            Unreachable();
    }
}

inline size_t compiled_size(const ArgVariant& varg, const CodeGenerator& codegen)
{
    return visit_one(varg, [&](const auto& arg) { return ::compiled_size(arg, codegen); });
}

inline size_t compiled_size(const CompiledCommand& cmd, const CodeGenerator& codegen)
{
    size_t size = sizeof(uint16_t);
    for(auto& a : cmd.args) size += ::compiled_size(a, codegen);
    return size;
}

inline size_t CompiledScmHeader::compiled_size() const
{
    switch(this->version)
    {
        case CompiledScmHeader::Version::Liberty:
        case CompiledScmHeader::Version::Miami:
        {
            auto size_globals = this->size_global_vars_space;
            return 8 + (size_globals - 8) + 8 + 4 + (24 * (1 + this->models.size()))
                + 8 + 4 + 4 + 2 + 2 + (4 * this->num_missions);
            break;
        }
        case CompiledScmHeader::Version::SanAndreas:
        {
            auto size_globals = this->size_global_vars_space;
            return 8 + (size_globals - 8) + 8 + 4 + (24 * (1 + this->models.size()))
                + 8 + 4 + 4 + 2 + 2 + (4 * this->num_missions) + 4 //<SA
                + 8 + 4 + 4 + (28 * (1 + this->num_streamed))
                + 8 + 4 + 8 + 4 + 1 + 1 + 2;
            break;
        }
        default:
            Unreachable();
    }
}

inline size_t compiled_size(const CompiledData& data, const CodeGenerator& codegen)
{
    return visit_one(data.data, [&](const auto& data) { return ::compiled_size(data, codegen); });
}

/********/

template<typename T, typename CodeGen>
inline void generate_code(const T& x, CodeGen& codegen)
{
    return x.generate_code();
}

inline void generate_code(const EOAL&, CodeGenerator& codegen)
{
    codegen.emplace_u8(0);
}

inline void generate_code(const int8_t& value, CodeGenerator& codegen)
{
    codegen.emplace_u8(4);
    codegen.emplace_i8(value);
}

inline void generate_code(const int16_t& value, CodeGenerator& codegen)
{
    codegen.emplace_u8(5);
    codegen.emplace_i16(value);
}

inline void generate_code(const int32_t& value, CodeGenerator& codegen)
{
    codegen.emplace_u8(1);
    codegen.emplace_i32(value);
}

inline void generate_code(const float& value, CodeGenerator& codegen)
{
    if(codegen.program.opt.optimize_zero_floats && value == 0.0f)
    {
        generate_code(static_cast<int8_t>(0), codegen);
    }
    else if(codegen.program.opt.use_half_float)
    {
        codegen.emplace_u8(6);
        codegen.emplace_i16(static_cast<int16_t>(value * 16.0f));
    }
    else
    {
        static_assert(std::numeric_limits<float>::is_iec559
            && sizeof(float) == sizeof(uint32_t), "IEEE 754 floating point expected.");

        codegen.emplace_u8(6);
        codegen.emplace_u32(reinterpret_cast<const uint32_t&>(value));
    }
}

inline void generate_code(const shared_ptr<Label>& label_ptr, CodeGenerator& codegen)
{
    codegen.emplace_u8(1);

    auto emplace_local_offset = [&](int32_t offset)
    {
        if(offset == 0)
            codegen.program.error(nocontext, "XXX reference to zero offset");
        codegen.emplace_i32(-offset);
    };

    if(codegen.program.opt.use_local_offsets)
    {
        int32_t absolute_offset = static_cast<int32_t>(label_ptr->offset());
        emplace_local_offset(absolute_offset);
    }
    else if(label_ptr->script->type == ScriptType::Mission
         || label_ptr->script->type == ScriptType::StreamedScript)
    {
        assert(label_ptr->script == codegen.script); // enforced on compiler.hpp/cpp

        int32_t local_offset = static_cast<int32_t>(label_ptr->local_offset.value());
        emplace_local_offset(local_offset);
    }
    else
    {
        codegen.emplace_i32(label_ptr->offset());
    }
}

inline void generate_code(const CompiledString& str, CodeGenerator& codegen)
{
    switch(str.type)
    {
        case CompiledString::Type::TextLabel8:
            Expects(str.storage.size() <= 8);  // enforced on annotation
            if(codegen.program.opt.has_text_label_prefix)
                codegen.emplace_u8(9);
            codegen.emplace_chars(8, str.storage.c_str());
            break;
        case CompiledString::Type::TextLabel16:
            Expects(str.storage.size() <= 16); // enforced on annotation
            codegen.emplace_u8(0xF);
            codegen.emplace_chars(16, str.storage.c_str());
            break;
        case CompiledString::Type::StringVar:
            Expects(str.storage.size() <= 127);  // enforced on annotation
            codegen.emplace_u8(0xE);
            codegen.emplace_u8(static_cast<uint8_t>(str.storage.size()));
            codegen.emplace_chars(str.storage.size(), str.storage.c_str());
            break;
        case CompiledString::Type::String128:
            codegen.emplace_chars(128, str.storage.c_str());
            break;
        default:
            Unreachable();
    }
}

inline void generate_code(const CompiledVar& v, CodeGenerator& codegen)
{
    bool global = v.var->global;

    if(v.index == nullopt)
    {
        switch(v.var->type)
        {
            case VarType::Int:
            case VarType::Float:
                codegen.emplace_u8(global? 0x2 : 0x3);
                break;
            case VarType::TextLabel:
                codegen.emplace_u8(global? 0xA : 0xB);
                break;
            case VarType::TextLabel16:
                codegen.emplace_u8(global? 0x10 : 0x11);
                break;
            default:
                Unreachable();
        }

        codegen.emplace_u16(static_cast<uint16_t>(global? v.var->offset() : v.var->index));
    }
    else
    {
        if(is<int32_t>(*v.index))
        {
            switch(v.var->type)
            {
                case VarType::Int:
                case VarType::Float:
                    codegen.emplace_u8(global? 0x2 : 0x3);
                    break;
                case VarType::TextLabel:
                    codegen.emplace_u8(global? 0xA : 0xB);
                    break;
                case VarType::TextLabel16:
                    codegen.emplace_u8(global? 0x10 : 0x11);
                    break;
                default:
                    Unreachable();
            }

            codegen.emplace_u16(static_cast<uint16_t>(global? v.var->offset() + get<int32_t>(*v.index) * 4 : v.var->index + get<int32_t>(*v.index)));
        }
        else
        {
            auto& indexVar = get<shared_ptr<Var>>(*v.index);
            switch(v.var->type)
            {
                case VarType::Int:
                case VarType::Float:
                    codegen.emplace_u8(global? 0x7 : 0x8);
                    break;
                case VarType::TextLabel:
                    codegen.emplace_u8(global? 0xC : 0xD);
                    break;
                case VarType::TextLabel16:
                    codegen.emplace_u8(global? 0x12 : 0x13);
                    break;
                default:
                    Unreachable();
            }

            codegen.emplace_u16(static_cast<uint16_t>(global? v.var->offset() : v.var->index));
            codegen.emplace_u16(static_cast<uint16_t>(indexVar->global? indexVar->offset() : indexVar->index));
            codegen.emplace_u8(static_cast<uint8_t>(v.var->count.value()));
            codegen.emplace_u8((static_cast<uint8_t>(v.var->type) & 0x7F) | (indexVar->global << 7));
        }
    }
}

inline void generate_code(const ArgVariant& varg, CodeGenerator& codegen)
{
    return visit_one(varg, [&](const auto& arg) { return ::generate_code(arg, codegen); });
}

inline void generate_code(const CompiledCommand& ccmd, CodeGenerator& codegen)
{
    codegen.emplace_u16(ccmd.id);
    for(auto& arg : ccmd.args) ::generate_code(arg, codegen);
}

inline void generate_code(const CompiledLabelDef&, CodeGenerator&)
{
    // label definitions do not have a physical representation
}

inline void generate_code(const CompiledHex& hex, CodeGenerator& codegen)
{
    codegen.emplace_bytes(hex.data.size(), hex.data.data());
}

inline void generate_code(const CompiledScmHeader& header, CodeGeneratorData& codegen)
{
    assert(header.version == CompiledScmHeader::Version::Liberty
        || header.version == CompiledScmHeader::Version::Miami
        || header.version == CompiledScmHeader::Version::SanAndreas);

    auto nextseg_id = [v = header.version, current_segid = 0u]() mutable
    {
        if(v == CompiledScmHeader::Version::SanAndreas)
            return current_segid++;
        return 0u;
    };

    auto goto_rel = [&codegen](int32_t skip_bytes)
    {
        int32_t target = 8 + skip_bytes + codegen.current_offset();
        codegen.emplace_u16(0x0002);
        codegen.emplace_u8(1);
        codegen.emplace_i32(target);
    };

    auto comp_largest_mission = [](const shared_ptr<const Script>& m1, const shared_ptr<const Script>& m2)
    {
        return m1->size.value() < m2->size.value();
    };

    uint32_t head_size            = header.compiled_size();
    uint32_t main_size            = head_size;
    uint32_t multifile_size       = head_size;
    uint32_t largest_mission_size = 0;
    uint32_t largest_streamed_size = 0 ;

    std::vector<shared_ptr<const Script>> missions;
    std::vector<shared_ptr<const Script>> streameds;

    char target_id = (header.version == CompiledScmHeader::Version::Liberty? '\0' : // original III main.scm doesn't use 'l' yet
                      header.version == CompiledScmHeader::Version::Miami? 'm' :
                      header.version == CompiledScmHeader::Version::SanAndreas? 's' :
                      Unreachable());

    missions.reserve(header.num_missions);
    streameds.reserve(header.num_streamed);

    for(auto& sc : header.scripts)
    {
        if(sc->type == ScriptType::Mission)
        {
            missions.emplace_back(sc);
            multifile_size += sc->size.value();
            if(largest_mission_size < sc->size.value())
                largest_mission_size = *sc->size;
        }
        else if(sc->type == ScriptType::StreamedScript)
        {
            streameds.emplace_back(sc);
            if(largest_streamed_size < sc->size.value())
                largest_streamed_size = *sc->size;
        }
        else
        {
            main_size += sc->size.value();
            multifile_size += sc->size.value();
        }
    }

    // Variables segment
    auto size_globals = header.size_global_vars_space;
    goto_rel(size_globals - 8);
    codegen.emplace_i8(target_id);
    codegen.emplace_fill(size_globals - 8, 0);

    // Models segment
    goto_rel(4 + (24 * (1 + header.models.size())));
    codegen.emplace_u8(nextseg_id());
    codegen.emplace_u32(1 + header.models.size());
    codegen.emplace_chars(24, "");
    for(auto& model : header.models)
        codegen.emplace_chars(24, model.c_str());

    // SCM info segment
    {
        int32_t rel_offset = 4 + 4 + 2 + 2 + (4 * missions.size()) +
                            (header.version == CompiledScmHeader::Version::SanAndreas? 4 : 0);
        goto_rel(rel_offset);
        codegen.emplace_u8(nextseg_id());
        codegen.emplace_u32(main_size);
        codegen.emplace_u32(largest_mission_size);
        codegen.emplace_u16(static_cast<uint16_t>(missions.size()));
        codegen.emplace_u16(0); // number_of_exclusive_missions

        if(header.version == CompiledScmHeader::Version::SanAndreas)
        {
            codegen.emplace_u32(0); // TODO Highest number of locals used in mission
        }

        for(auto& script_ptr : missions)
            codegen.emplace_i32(script_ptr->offset.value());
    }

    // Streamed scripts segment
    if(header.version == CompiledScmHeader::Version::SanAndreas)
    {
        uint32_t virtual_offset = multifile_size;

        goto_rel(4 + 4 + (28 * (1 + streameds.size())));
        codegen.emplace_u8(nextseg_id());
        codegen.emplace_u32(largest_streamed_size);
        codegen.emplace_u32(static_cast<uint32_t>(1 + streameds.size()));

        for(auto& script_ptr : streameds)
        {
            auto name = script_ptr->path.stem().u8string();
            std::transform(name.begin(), name.end(), name.begin(), ::toupper); // TODO UTF-8 able?
            codegen.emplace_chars(20, name.c_str());
            codegen.emplace_u32(virtual_offset);
            codegen.emplace_u32(*script_ptr->size);
            virtual_offset += *script_ptr->size;
        }

        // AAA script
        {
            codegen.emplace_chars(20, "AAA");
            codegen.emplace_u32(0);
            codegen.emplace_u32(8);
        }
    }

    // Unknown segment
    if(header.version == CompiledScmHeader::Version::SanAndreas)
    {
        goto_rel(4);
        codegen.emplace_u8(nextseg_id());
        codegen.emplace_u32(0);
    }

    // Unknown segment 2
    if(header.version == CompiledScmHeader::Version::SanAndreas)
    {
        goto_rel(4 + 1 + 1 + 2);
        codegen.emplace_u8(nextseg_id());
        codegen.emplace_u32(size_globals - 8);
        codegen.emplace_u8(62);  // TODO Number of allocated externals (with 07D3, 0884, 0928 or 0929) 
        codegen.emplace_u8(2);  // Unknown / Unused
        codegen.emplace_u16(0); // Unknown / Unused
    }

}

inline void generate_code(const CompiledData& data, CodeGenerator& codegen)
{
    return visit_one(data.data, [&](const auto& data) { return ::generate_code(data, codegen); });
}

