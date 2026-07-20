from .utils import write_file_lazy, ensure_dir

types = ["u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "f32", "f64"]
sizes = [2, 3, 4]
unary_ops = ["-"]
binary_ops = ["*", "/", "+", "-"]
variables = ["x", "y", "z", "w"]
unary_std_ops = ["abs", "floor", "ceil", "round"]
binary_std_ops = ["min", "max", "copysign"]

def generate_math(build_dir):
    defs_out = ""
    ops_out = ""
    for size in sizes:

        members = [variables[i] for i in range(0, size)]
        vec = f"Vec<{size}, T>"

        # ---- vec-defs.inl ----

        defs_out += f"template<typename T> struct {vec} {{\n"

        # members
        defs_out += f"    T {", ".join(members)};\n"

        # comparison
        defs_out +=  "    constexpr auto operator<=>(const Vec&) const noexcept -> std::strong_ordering = default;\n"

        # indexing
        index_op = f"switch (i) {{ {" ".join([f"case {i}: return {v};" for i, v in enumerate(members)])} }}; std::unreachable();"
        defs_out += f"    constexpr auto operator[](usz i) const -> T {{ {index_op} }}\n"
        defs_out += f"    constexpr auto operator[](usz i) -> T& {{ {index_op} }}\n"

        # member operations
        for op in binary_ops:
            # op= scalar
            op_string = "; ".join([f"{v} {op}= s" for v in members])
            defs_out += f"    constexpr decltype(auto) operator{op}=(T s) {{ {op_string}; return *this; }}\n"

            # op= vec
            op_string = "; ".join([f"{v} {op}= v.{v}" for v in members])
            defs_out += f"    constexpr decltype(auto) operator{op}=(Vec v) {{ {op_string}; return *this; }}\n"

        op_string = " || ".join([f"bool({v})" for v in members])
        defs_out += f"    constexpr explicit operator bool() const noexcept {{ return {op_string}; }}\n"

        defs_out += "};\n"

        # aliases
        for type in types:
            defs_out += f"using vec{size}{type} = Vec<{size}, {type}>;\n"

        # ---- vec-ops.inl ----

        # casting
        op_string = ", ".join([f"To(v.{v})" for v in members])
        ops_out += f"template<typename To, typename From> constexpr auto vec_cast(Vec<{size}, From> v) -> Vec<{size}, To> {{ return {{ {op_string} }}; }}\n"

        preamble = "template<typename T> constexpr auto"

        # free unary ops
        for op in unary_ops:
            op_string = ", ".join([f"{op} v.{v}" for v in members])
            ops_out += f"{preamble} operator{op}({vec} v) -> {vec} {{ return {{{op_string}}}; }}\n"

        # free binary ops
        for op in binary_ops:

            # vec op vec
            op_string = ", ".join([f"T(a.{v} {op} b.{v})" for v in members])
            ops_out += f"{preamble} operator{op}({vec} a, {vec} b) -> {vec} {{ return {{{op_string}}}; }}\n"

            # vec op scalar
            op_string = ", ".join([f"T(a.{v} {op} b)" for v in members])
            ops_out += f"{preamble} operator{op}({vec} a, T b) -> {vec} {{ return {{{op_string}}}; }}\n"

            # scalar op vec
            op_string = ", ".join([f"T(a {op} b.{v})" for v in members])
            ops_out += f"{preamble} operator{op}(T a, {vec} b) -> {vec} {{ return {{{op_string}}}; }}\n"

        for op in unary_std_ops:
            op_string = ", ".join([f"T(std::{op}(v.{v}))" for v in members])
            ops_out += f"{preamble} vec_{op}({vec} v) -> {vec} {{ return {{ {op_string} }}; }}\n"

        for op in binary_std_ops:
            op_string = ", ".join([f"T(std::{op}(a.{v}, b.{v}))" for v in members])
            ops_out += f"{preamble} vec_{op}({vec} a, {vec} b) -> {vec} {{ return {{ {op_string} }}; }}\n"

        # extra
        ops_out += f"{preamble} vec_clamp({vec} v, {vec} min, {vec} max) -> {vec} {{ return vec_max(vec_min(v, max), min); }}\n"
        ops_out += f"{preamble} vec_round_to_zero({vec} v) -> {vec} {{ return vec_copysign(vec_floor(vec_abs(v)), v); }}\n"
        ops_out += f"{preamble} vec_magnitude({vec} v) -> T {{ return std::sqrt({" + ".join([f"v.{v} * v.{v}" for v in members])}); }}\n"
        ops_out += f"{preamble} vec_distance({vec} a, {vec} b) -> T {{ return vec_magnitude(a - b); }}\n"

        # printing
        ops_out +=  "template<typename T>\n"
        ops_out += f"struct std::formatter<{vec}> {{\n"
        ops_out +=  "    constexpr auto parse(auto& ctx) { return ctx.begin(); }\n"
        ops_out += f"    constexpr auto format(const {vec}& v, auto& ctx) const {{\n"
        ops_out += f"        return std::format_to(ctx.out(), \"({", ".join(["{}" for i in range(0, size)])})\", {", ".join([f"v.{v}" for v in members])});\n"
        ops_out +=  "    }\n"
        ops_out +=  "};\n"

    math_dir = ensure_dir(build_dir / "math")
    write_file_lazy(math_dir / "vec-defs.inl", defs_out)
    write_file_lazy(math_dir / "vec-ops.inl", ops_out)
