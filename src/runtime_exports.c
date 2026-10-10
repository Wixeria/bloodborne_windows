/* Import-name lookup is shared by independently testable runtime components. */
#include "runtime.h"
#include <string.h>

static const struct { const char *nid, *symbol; } import_names[]={
#include "import_names.inc"
};
static const struct { const char *nid, *symbol; } contract_names[]={
#include "runtime_contract_names.inc"
};
const char *runtime_symbol(const char *nid) {
    for (size_t i=0;i<sizeof(import_names)/sizeof(*import_names);++i)
        if (!strcmp(nid,import_names[i].nid)) return import_names[i].symbol;
    /* Additional implemented contracts use the same known library scopes.
       Keep scope matching exact: a symbol imported from a different library
       must not accidentally resolve to this implementation. */
    for (size_t i=0;i<sizeof(contract_names)/sizeof(*contract_names);++i)
        if (!strcmp(nid,contract_names[i].nid)) return contract_names[i].symbol;
    return NULL;
}
/* Same symbol name means same contract whether imported from libkernel or
 * libScePosix, so newer modules resolve by name through runtime_symbol. */
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *nid) {
    const char *symbol=runtime_symbol(nid);
    if (!symbol) return 0;
    for (size_t i=0;i<count;++i) if (!strcmp(symbol,table[i].name)) return (uintptr_t)table[i].function;
    return 0;
}
const char *runtime_import_name(const char *name) {
    const char *symbol=runtime_symbol(name);
    return symbol ? symbol : "name not resolved; see analysis.json import_name_hints";
}
