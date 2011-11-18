#include <stdlib.h>
#include <string.h>
#include <unwind.h>
#include <stdint.h>
#include <stdbool.h>
#include "dwarf.h"
#include <stdio.h>
#include "abortf.h"
#include "core_types.h"
#include "runtime.h"

static uint64_t pup_unwindclass
	= ((uint64_t)'H')<<56
	| ((uint64_t)'o')<<48
	| ((uint64_t)'l')<<40
	| ((uint64_t)'r')<<32
	| ((uint64_t)'P')<<24
	| ((uint64_t)'u')<<16
	| ((uint64_t)'p')<<8
	| ((uint64_t)'\000');

struct PupUnwindException {
	struct PupObject *pup_exception;
	struct _Unwind_Exception unwindException;
};

extern struct PupClass ExceptionClassInstance;

static void pup_uwind_exception_cleanup(
	const _Unwind_Reason_Code reason,
	struct _Unwind_Exception* cleanup
) {
	// TODO?
	//fprintf(stderr, "pup_uwind_exception_cleanup(reason:%d, cleanup:%p\n)",
	//        reason, cleanup);
}

static struct _Unwind_Exception *create_unwind_exception(
	struct PupObject *pup_exception_obj
) {
	size_t size = sizeof(struct PupUnwindException);

	struct PupUnwindException* ret
		= (struct PupUnwindException*)memset(malloc(size), 0, size);
	ret->pup_exception = pup_exception_obj;
	ret->unwindException.exception_class = pup_unwindclass;
	ret->unwindException.exception_cleanup = pup_uwind_exception_cleanup;
	return &(ret->unwindException);
}

struct PupObject *extract_exception_obj(
	const struct _Unwind_Exception *exceptionObject
) {
	// TODO: move somewere sensible?
	const struct PupUnwindException dummyException;
	const int64_t baseFromUnwindOffset = ((uintptr_t) &dummyException) - 
		((uintptr_t) &(dummyException.unwindException));
	struct PupUnwindException *ue = (struct PupUnwindException *)
		(((void *) exceptionObject) + baseFromUnwindOffset);
	return ue->pup_exception;
}

void pup_raise(struct PupObject *pup_exception)
{
	struct _Unwind_Exception *e = create_unwind_exception(pup_exception);
	_Unwind_Reason_Code reason = _Unwind_RaiseException(e);
	ABORT_ON(reason == _URC_END_OF_STACK, "Exception was not caught");
	ABORTF("_Unwind_RaiseException() returned with %d", reason);
}

void pup_rethrow_uncaught_exception(struct _Unwind_Exception *dw_excep)
{
	_Unwind_Reason_Code reason = _Unwind_RaiseException(dw_excep);
	ABORTF("Unexpectedly failed to rethrow exception; reason code:%d",
	       reason);
}

/// read a uleb128 encoded value and advance pointer 
/// See Variable Length Data in: 
/// @link http://dwarfstd.org/Dwarf3.pdf @unlink
/// @param data reference variable holding memory pointer to decode from
/// @returns decoded value
/// 
static uintptr_t readULEB128(const uint8_t** data)
{
    uintptr_t result = 0;
    uintptr_t shift = 0;
    unsigned char byte;
    const uint8_t* p = *data;

    do 
    {
        byte = *p++;
        result |= (byte & 0x7f) << shift;
        shift += 7;
    } 
    while (byte & 0x80);

    *data = p;

    return result;
}


/// read a sleb128 encoded value and advance pointer 
/// See Variable Length Data in: 
/// @link http://dwarfstd.org/Dwarf3.pdf @unlink
/// @param data reference variable holding memory pointer to decode from
/// @returns decoded value
/// 
static uintptr_t readSLEB128(const uint8_t** data)
{
    uintptr_t result = 0;
    uintptr_t shift = 0;
    unsigned char byte;
    const uint8_t* p = *data;

    do 
    {
        byte = *p++;
        result |= (byte & 0x7f) << shift;
        shift += 7;
    } 
    while (byte & 0x80);

    *data = p;

    // Note: Not sure this works
    //
    if ((byte & 0x40) && (shift < (sizeof(result) << 3)))
    {
        // Note: I wonder why I can't do: ~0 << shift
        //
        // result |= -(1L << shift);
        result |= (~0 << shift);
    }

    return result;
}


/// read a pointer encoded value and advance pointer 
/// See Variable Length Data in: 
/// @link http://dwarfstd.org/Dwarf3.pdf @unlink
/// @param data reference variable holding memory pointer to decode from
/// @param encoding dwarf encoding type
/// @returns decoded value
///
static uintptr_t readEncodedPointer(const uint8_t** data, const uint8_t encoding)
{
    const uint8_t* p = *data;
    uintptr_t result = 0;

    if (encoding != DW_EH_PE_omit) 
    {
        // first get value 
        switch (encoding & 0x0F) 
        {
            case DW_EH_PE_absptr:
                result = *((uintptr_t*)p);
                p += sizeof(uintptr_t);
                break;
            case DW_EH_PE_uleb128:
                result = readULEB128(&p);
                break;
            // Note: This case has not been tested
            case DW_EH_PE_sleb128:
                result = readSLEB128(&p);
                break;
            case DW_EH_PE_udata2:
                result = *((uint16_t*)p);
                p += sizeof(uint16_t);
                break;
            case DW_EH_PE_udata4:
                result = *((uint32_t*)p);
                p += sizeof(uint32_t);
                break;
            case DW_EH_PE_udata8:
                result = *((uint64_t*)p);
                p += sizeof(uint64_t);
                break;
            case DW_EH_PE_sdata2:
                result = *((int16_t*)p);
                p += sizeof(int16_t);
                break;
            case DW_EH_PE_sdata4:
                result = *((int32_t*)p);
                p += sizeof(int32_t);
                break;
            case DW_EH_PE_sdata8:
                result = *((int64_t*)p);
                p += sizeof(int64_t);
                break;
            default:
                // not supported 
                abort();
                break;
        }

        // then add relative offset 
        switch ( encoding & 0x70 ) 
        {
            case DW_EH_PE_absptr:
                // do nothing 
                break;
            case DW_EH_PE_pcrel:
                result += (uintptr_t)(*data);
                break;
            case DW_EH_PE_textrel:
            case DW_EH_PE_datarel:
            case DW_EH_PE_funcrel:
            case DW_EH_PE_aligned:
            default:
                // not supported 
                abort();
                break;
        }

        // then apply indirection 
        if (encoding & DW_EH_PE_indirect) 
        {
            result = *((uintptr_t*)result);
        }

        *data = p;
    }

    return result;
}

static bool is_foreign_unwindclass(const uint64_t unwindclass)
{
	return unwindclass != pup_unwindclass;
}

static bool handle_action_value(int64_t *resultAction,
                              struct PupClass **exception_class_table, 
                              const uintptr_t action_table_entry, 
                              const uint64_t exceptionClass, 
                              const struct _Unwind_Exception *exceptionObject)
{
	if (!resultAction || !exceptionObject
	    || is_foreign_unwindclass(exceptionClass))
	{
		return false;
	}

	const uint8_t *actionPos = (uint8_t*) action_table_entry,
	              *tempActionPos;
	int64_t typeOffset = 0,
	        actionOffset;

	for (int i = 0; true; ++i) {
		// Each emitted dwarf action corresponds to a 2 tuple of
		// type info address offset, and action offset to the next
		// emitted action.
		typeOffset = readSLEB128(&actionPos);
		tempActionPos = actionPos;
		actionOffset = readSLEB128(&tempActionPos);

		ABORT_ON(typeOffset < 0,
		         "handleActionValue(...):filters are not supported.");
		// Note: A typeOffset == 0 implies that a cleanup
		//       llvm.eh.selector argument has been matched.
		if ((typeOffset > 0) &&
		     true)  /* TODO: table contents are not as expected
				and trying to test the table entry segfaults
				for me.  Since we currently just catch all
				exceptions and defer real type-testing to
				runtime, we can get away with '&& true' here*/
		    //(pup_is_descendant_or_same(type, exception_class_table[-typeOffset])))
		{
		    *resultAction = i + 1;
		    return true;
		}
		
		if (!actionOffset) {
		    break;
		}
		
		actionPos += actionOffset;
	}

	return false;
}

struct UnwindCallsite {
	uintptr_t start;
	uintptr_t length;
	uintptr_t landingPad;
	uintptr_t action;
};

static void unwind_callsite_read(struct UnwindCallsite *callsite,
                                 const uint8_t** p,
                                 const uint8_t encoding)
{
	callsite->start = readEncodedPointer(p, encoding);
	callsite->length = readEncodedPointer(p, encoding);
	callsite->landingPad = readEncodedPointer(p, encoding);
	callsite->action = readULEB128(p);
}

static bool unwind_callsite_contains(struct UnwindCallsite *callsite,
                                     uintptr_t pcOffset)
{
	return (callsite->start <= pcOffset)
	    && (pcOffset < (callsite->start+callsite->length));
}

static _Unwind_Reason_Code handle_lsda(const uint8_t *lsda,
                                       const _Unwind_Action actions,
                                       const uint64_t unwindclass,
                                       const struct _Unwind_Exception *ue_header,
                                       struct _Unwind_Context *context)
{
	uintptr_t pc = _Unwind_GetIP(context)-1;
	uintptr_t funcStart = _Unwind_GetRegionStart(context);
	uintptr_t pcOffset = pc - funcStart;
	struct PupClass** exception_class_table = NULL;

	uint8_t lpStartEncoding = *lsda++;
	if (lpStartEncoding != DW_EH_PE_omit) {
		readEncodedPointer(&lsda, lpStartEncoding);
	}
	uint8_t ttypeEncoding = *lsda++;
	if (ttypeEncoding != DW_EH_PE_omit) {
		uintptr_t classInfoOffset = readULEB128(&lsda);
		// exception_class_table points just past the end of the array
		// FIXME: the resulting table entries seem to be incorrect,
		//        which must mean I've done something wrong!  (only 32
		//        bits of the address; on my 64bit machine?)
		exception_class_table = (struct PupClass**) (lsda + classInfoOffset);
	}
	/* Walk call-site table looking for range that includes current PC. */
	uint8_t         callSiteEncoding = *lsda++;
	uint32_t        callSiteTableLength = readULEB128(&lsda);
	const uint8_t*  callSiteTableStart = lsda;
	const uint8_t*  callSiteTableEnd = callSiteTableStart + callSiteTableLength;
	const uintptr_t  action_table_start = (uintptr_t)callSiteTableEnd;
	const uint8_t* p = callSiteTableStart;
	while (p < callSiteTableEnd) {
		struct UnwindCallsite callsite;
		unwind_callsite_read(&callsite, &p, callSiteEncoding);

		if (!callsite.landingPad) {
			continue; /* no landing pad for this entry */
		}
		uintptr_t action_table_entry = 0;
		// don't consider actions (rescue-blocks) for foreign
		// exceptions
		if (!is_foreign_unwindclass(unwindclass) && callsite.action) {
			action_table_entry = action_table_start + callsite.action - 1;
		}
		if (unwind_callsite_contains(&callsite, pcOffset)) {
			bool exceptionMatched = false;
			int64_t actionValue = 0;
			if (action_table_entry) {
				exceptionMatched = handle_action_value(
			    				&actionValue,
			    				exception_class_table, 
			    				action_table_entry, 
			    				unwindclass, 
			    				ue_header
				                   );
			}
			if (!(actions & _UA_SEARCH_PHASE)) {
				/* Found landing pad for the PC.
				 * Set Instruction Pointer to so we re-enter
				 * function at landing pad. The landing pad is
				 * created by the compiler to take two
				 * parameters in registers.
				 */
				_Unwind_SetGR(context,
					      __builtin_eh_return_data_regno(0), 
					      (uintptr_t)ue_header);
				if (!action_table_entry || !exceptionMatched) {
					_Unwind_SetGR(context,
						      __builtin_eh_return_data_regno(1),
						      0);
				} else {
					_Unwind_SetGR(context,
					              __builtin_eh_return_data_regno(1),
						      actionValue);
				}
				_Unwind_SetIP(context, funcStart+callsite.landingPad);
				return _URC_INSTALL_CONTEXT;
			}
			if (exceptionMatched) {
				return _URC_HANDLER_FOUND;
			}
		}
	}
	
	/* No landing pad found, continue unwinding. */
	return _URC_CONTINUE_UNWIND;
}

_Unwind_Reason_Code pup_eh_personality(const int version,
                        const _Unwind_Action actions,
                        const uint64_t unwindclass,
                        const struct _Unwind_Exception *ue_header,
                        struct _Unwind_Context *context)
{
	const uint8_t *lsda;

	if (version != 1) {
		// unexpected version; seems best not to try handling
		return _URC_CONTINUE_UNWIND;
	}

	lsda = (const uint8_t *)_Unwind_GetLanguageSpecificData(context);
	if (!lsda) {
		// no metadata for this stack frame; nothing to do
		return _URC_CONTINUE_UNWIND;
	}

	return handle_lsda(lsda, actions, unwindclass, ue_header, context);
}
