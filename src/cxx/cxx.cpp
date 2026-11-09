#include "kr32/cxx/cxx.h"

#include "cxx/codegen.h"
#include "cxx/parser.h"

#include "kr32/asm/object_assembler.h"
#include "kr32/asm/optimizer.h"
#include "kr32/container/container.h"

#include <memory>
#include <utility>

namespace kr32::cxx {

Result compile(std::string_view source, const Options& options) {
    Result result;
    const std::shared_ptr<ast::TranslationUnit> unit =
        parse(source, options.source_name, result.diagnostics);
    if (!result.diagnostics.empty()) {
        return result;
    }

    result.assembly = generate_assembly(*unit, result.diagnostics);
    if (!result.diagnostics.empty()) {
        result.assembly.clear();
        return result;
    }
    if (options.optimize) {
        const assembler::OptimizeStats stats =
            assembler::optimize_assembly(result.assembly);
        (void)stats;
    }
    if (options.emit == Emit::Object) {
        container::Image object_image;
        assembler::Error assembly_error;
        if (!assembler::assemble_object(result.assembly, object_image, assembly_error)) {
            Diagnostic diagnostic;
            diagnostic.line = 1;
            diagnostic.column = 1;
            diagnostic.message = assembly_error.message;
            result.diagnostics.push_back(std::move(diagnostic));
            result.assembly.clear();
            return result;
        }
        container::Error container_error;
        if (!container::write(object_image, result.object, container_error)) {
            Diagnostic diagnostic;
            diagnostic.line = 1;
            diagnostic.column = 1;
            diagnostic.message = container_error.message;
            result.diagnostics.push_back(std::move(diagnostic));
            result.assembly.clear();
            result.object.clear();
            return result;
        }
        result.assembly.clear();
    }
    result.ok = true;
    return result;
}

} // namespace kr32::cxx
