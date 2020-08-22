/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * vim9execute.c: execute Vim9 script instructions
 */

#define USING_FLOAT_STUFF
#include "vim.h"

#if defined(FEAT_EVAL) || defined(PROTO)

#ifdef VMS
# include <float.h>
#endif

#include "vim9.h"

// Structure put on ec_trystack when ISN_TRY is encountered.
typedef struct {
    int	    tcd_frame_idx;	// ec_frame_idx when ISN_TRY was encountered
    int	    tcd_catch_idx;	// instruction of the first catch
    int	    tcd_finally_idx;	// instruction of the finally block
    int	    tcd_caught;		// catch block entered
    int	    tcd_return;		// when TRUE return from end of :finally
} trycmd_T;


// A stack is used to store:
// - arguments passed to a :def function
// - info about the calling function, to use when returning
// - local variables
// - temporary values
//
// In detail (FP == Frame Pointer):
//	  arg1		first argument from caller (if present)
//	  arg2		second argument from caller (if present)
//	  extra_arg1	any missing optional argument default value
// FP ->  cur_func	calling function
//        current	previous instruction pointer
//        frame_ptr	previous Frame Pointer
//        var1		space for local variable
//        var2		space for local variable
//        ....		fixed space for max. number of local variables
//        temp		temporary values
//        ....		flexible space for temporary values (can grow big)

/*
 * Execution context.
 */
typedef struct {
    garray_T	ec_stack;	// stack of typval_T values
    int		ec_frame_idx;	// index in ec_stack: context of ec_dfunc_idx

    garray_T	*ec_outer_stack;    // stack used for closures
    int		ec_outer_frame;	    // stack frame in ec_outer_stack

    garray_T	ec_trystack;	// stack of trycmd_T values
    int		ec_in_catch;	// when TRUE in catch or finally block

    int		ec_dfunc_idx;	// current function index
    isn_T	*ec_instr;	// array with instructions
    int		ec_iidx;	// index in ec_instr: instruction to execute
} ectx_T;

// Get pointer to item relative to the bottom of the stack, -1 is the last one.
#define STACK_TV_BOT(idx) (((typval_T *)ectx->ec_stack.ga_data) + ectx->ec_stack.ga_len + idx)

    void
to_string_error(vartype_T vartype)
{
    semsg(_("E1105: Cannot convert %s to string"), vartype_name(vartype));
}

/*
 * Return the number of arguments, including optional arguments and any vararg.
 */
    static int
ufunc_argcount(ufunc_T *ufunc)
{
    return ufunc->uf_args.ga_len + (ufunc->uf_va_name != NULL ? 1 : 0);
}

/*
 * Set the instruction index, depending on omitted arguments, where the default
 * values are to be computed.  If all optional arguments are present, start
 * with the function body.
 * The expression evaluation is at the start of the instructions:
 *  0 ->  EVAL default1
 *	       STORE arg[-2]
 *  1 ->  EVAL default2
 *	       STORE arg[-1]
 *  2 ->  function body
 */
    static void
init_instr_idx(ufunc_T *ufunc, int argcount, ectx_T *ectx)
{
    if (ufunc->uf_def_args.ga_len == 0)
	ectx->ec_iidx = 0;
    else
    {
	int	defcount = ufunc->uf_args.ga_len - argcount;

	// If there is a varargs argument defcount can be negative, no defaults
	// to evaluate then.
	if (defcount < 0)
	    defcount = 0;
	ectx->ec_iidx = ufunc->uf_def_arg_idx[
					 ufunc->uf_def_args.ga_len - defcount];
    }
}

/*
 * Create a new list from "count" items at the bottom of the stack.
 * When "count" is zero an empty list is added to the stack.
 */
    static int
exe_newlist(int count, ectx_T *ectx)
{
    list_T	*list = list_alloc_with_items(count);
    int		idx;
    typval_T	*tv;

    if (list == NULL)
	return FAIL;
    for (idx = 0; idx < count; ++idx)
	list_set_item(list, idx, STACK_TV_BOT(idx - count));

    if (count > 0)
	ectx->ec_stack.ga_len -= count - 1;
    else if (GA_GROW(&ectx->ec_stack, 1) == FAIL)
	return FAIL;
    else
	++ectx->ec_stack.ga_len;
    tv = STACK_TV_BOT(-1);
    tv->v_type = VAR_LIST;
    tv->vval.v_list = list;
    ++list->lv_refcount;
    return OK;
}

/*
 * Call compiled function "cdf_idx" from compiled code.
 *
 * Stack has:
 * - current arguments (already there)
 * - omitted optional argument (default values) added here
 * - stack frame:
 *	- pointer to calling function
 *	- Index of next instruction in calling function
 *	- previous frame pointer
 * - reserved space for local variables
 */
    static int
call_dfunc(int cdf_idx, int argcount_arg, ectx_T *ectx)
{
    int	    argcount = argcount_arg;
    dfunc_T *dfunc = ((dfunc_T *)def_functions.ga_data) + cdf_idx;
    ufunc_T *ufunc = dfunc->df_ufunc;
    int	    arg_to_add;
    int	    vararg_count = 0;
    int	    idx;
    estack_T *entry;

    if (dfunc->df_deleted)
    {
	emsg_funcname(e_func_deleted, ufunc->uf_name);
	return FAIL;
    }

    if (ufunc->uf_va_name != NULL)
    {
	// Need to make a list out of the vararg arguments.
	// Stack at time of call with 2 varargs:
	//   normal_arg
	//   optional_arg
	//   vararg_1
	//   vararg_2
	// After creating the list:
	//   normal_arg
	//   optional_arg
	//   vararg-list
	// With missing optional arguments we get:
	//    normal_arg
	// After creating the list
	//    normal_arg
	//    (space for optional_arg)
	//    vararg-list
	vararg_count = argcount - ufunc->uf_args.ga_len;
	if (vararg_count < 0)
	    vararg_count = 0;
	else
	    argcount -= vararg_count;
	if (exe_newlist(vararg_count, ectx) == FAIL)
	    return FAIL;

	vararg_count = 1;
    }

    arg_to_add = ufunc->uf_args.ga_len - argcount;
    if (arg_to_add < 0)
    {
	iemsg("Argument count wrong?");
	return FAIL;
    }
    if (ga_grow(&ectx->ec_stack, arg_to_add + 3
		       + dfunc->df_varcount + dfunc->df_closure_count) == FAIL)
	return FAIL;

    // Move the vararg-list to below the missing optional arguments.
    if (vararg_count > 0 && arg_to_add > 0)
	*STACK_TV_BOT(arg_to_add - 1) = *STACK_TV_BOT(-1);

    // Reserve space for omitted optional arguments, filled in soon.
    for (idx = 0; idx < arg_to_add; ++idx)
	STACK_TV_BOT(idx - vararg_count)->v_type = VAR_UNKNOWN;
    ectx->ec_stack.ga_len += arg_to_add;

    // Store current execution state in stack frame for ISN_RETURN.
    STACK_TV_BOT(0)->vval.v_number = ectx->ec_dfunc_idx;
    STACK_TV_BOT(1)->vval.v_number = ectx->ec_iidx;
    STACK_TV_BOT(2)->vval.v_number = ectx->ec_frame_idx;
    ectx->ec_frame_idx = ectx->ec_stack.ga_len;

    // Initialize local variables
    for (idx = 0; idx < dfunc->df_varcount + dfunc->df_closure_count; ++idx)
	STACK_TV_BOT(STACK_FRAME_SIZE + idx)->v_type = VAR_UNKNOWN;
    ectx->ec_stack.ga_len += STACK_FRAME_SIZE
				+ dfunc->df_varcount + dfunc->df_closure_count;

    // Set execution state to the start of the called function.
    ectx->ec_dfunc_idx = cdf_idx;
    ectx->ec_instr = dfunc->df_instr;
    entry = estack_push_ufunc(dfunc->df_ufunc, 1);
    if (entry != NULL)
    {
	// Set the script context to the script where the function was defined.
	// TODO: save more than the SID?
	entry->es_save_sid = current_sctx.sc_sid;
	current_sctx.sc_sid = ufunc->uf_script_ctx.sc_sid;
    }

    // Decide where to start execution, handles optional arguments.
    init_instr_idx(ufunc, argcount, ectx);

    return OK;
}

// Get pointer to item in the stack.
#define STACK_TV(idx) (((typval_T *)ectx->ec_stack.ga_data) + idx)

/*
 * Used when returning from a function: Check if any closure is still
 * referenced.  If so then move the arguments and variables to a separate piece
 * of stack to be used when the closure is called.
 * When "free_arguments" is TRUE the arguments are to be freed.
 * Returns FAIL when out of memory.
 */
    static int
handle_closure_in_use(ectx_T *ectx, int free_arguments)
{
    dfunc_T	*dfunc = ((dfunc_T *)def_functions.ga_data)
							  + ectx->ec_dfunc_idx;
    int		argcount = ufunc_argcount(dfunc->df_ufunc);
    int		top = ectx->ec_frame_idx - argcount;
    int		idx;
    typval_T	*tv;
    int		closure_in_use = FALSE;

    // Check if any created closure is still in use.
    for (idx = 0; idx < dfunc->df_closure_count; ++idx)
    {
	tv = STACK_TV(ectx->ec_frame_idx + STACK_FRAME_SIZE
						   + dfunc->df_varcount + idx);
	if (tv->v_type == VAR_PARTIAL && tv->vval.v_partial != NULL
					&& tv->vval.v_partial->pt_refcount > 1)
	{
	    int refcount = tv->vval.v_partial->pt_refcount;
	    int i;

	    // A Reference in a local variables doesn't count, it gets
	    // unreferenced on return.
	    for (i = 0; i < dfunc->df_varcount; ++i)
	    {
		typval_T *stv = STACK_TV(ectx->ec_frame_idx
						       + STACK_FRAME_SIZE + i);
		if (stv->v_type == VAR_PARTIAL
				  && tv->vval.v_partial == stv->vval.v_partial)
		    --refcount;
	    }
	    if (refcount > 1)
	    {
		closure_in_use = TRUE;
		break;
	    }
	}
    }

    if (closure_in_use)
    {
	funcstack_T *funcstack = ALLOC_CLEAR_ONE(funcstack_T);
	typval_T    *stack;

	// A closure is using the arguments and/or local variables.
	// Move them to the called function.
	if (funcstack == NULL)
	    return FAIL;
	funcstack->fs_ga.ga_len = argcount + STACK_FRAME_SIZE
							  + dfunc->df_varcount;
	stack = ALLOC_CLEAR_MULT(typval_T, funcstack->fs_ga.ga_len);
	funcstack->fs_ga.ga_data = stack;
	if (stack == NULL)
	{
	    vim_free(funcstack);
	    return FAIL;
	}

	// Move or copy the arguments.
	for (idx = 0; idx < argcount; ++idx)
	{
	    tv = STACK_TV(top + idx);
	    if (free_arguments)
	    {
		*(stack + idx) = *tv;
		tv->v_type = VAR_UNKNOWN;
	    }
	    else
		copy_tv(tv, stack + idx);
	}
	// Move the local variables.
	for (idx = 0; idx < dfunc->df_varcount; ++idx)
	{
	    tv = STACK_TV(ectx->ec_frame_idx + STACK_FRAME_SIZE + idx);

	    // Do not copy a partial created for a local function.
	    // TODO: this won't work if the closure actually uses it.  But when
	    // keeping it it gets complicated: it will create a reference cycle
	    // inside the partial, thus needs special handling for garbage
	    // collection.
	    if (tv->v_type == VAR_PARTIAL && tv->vval.v_partial != NULL)
	    {
		int i;
		typval_T *ctv;

		for (i = 0; i < dfunc->df_closure_count; ++i)
		{
		    ctv = STACK_TV(ectx->ec_frame_idx + STACK_FRAME_SIZE
						     + dfunc->df_varcount + i);
		    if (tv->vval.v_partial == ctv->vval.v_partial)
			break;
		}
		if (i < dfunc->df_closure_count)
		{
		    (stack + argcount + STACK_FRAME_SIZE + idx)->v_type =
								   VAR_UNKNOWN;
		    continue;
		}
	    }

	    *(stack + argcount + STACK_FRAME_SIZE + idx) = *tv;
	    tv->v_type = VAR_UNKNOWN;
	}

	for (idx = 0; idx < dfunc->df_closure_count; ++idx)
	{
	    tv = STACK_TV(ectx->ec_frame_idx + STACK_FRAME_SIZE
						   + dfunc->df_varcount + idx);
	    if (tv->v_type == VAR_PARTIAL)
	    {
		partial_T *partial = tv->vval.v_partial;

		if (partial->pt_refcount > 1)
		{
		    ++funcstack->fs_refcount;
		    partial->pt_funcstack = funcstack;
		    partial->pt_ectx_stack = &funcstack->fs_ga;
		    partial->pt_ectx_frame = ectx->ec_frame_idx - top;
		}
	    }
	}
    }

    return OK;
}

/*
 * Return from the current function.
 */
    static int
func_return(ectx_T *ectx)
{
    int		idx;
    dfunc_T	*dfunc = ((dfunc_T *)def_functions.ga_data)
							  + ectx->ec_dfunc_idx;
    int		argcount = ufunc_argcount(dfunc->df_ufunc);
    int		top = ectx->ec_frame_idx - argcount;
    estack_T	*entry;

    // execution context goes one level up
    entry = estack_pop();
    if (entry != NULL)
	current_sctx.sc_sid = entry->es_save_sid;

    if (handle_closure_in_use(ectx, TRUE) == FAIL)
	return FAIL;

    // Clear the arguments.
    for (idx = top; idx < ectx->ec_frame_idx; ++idx)
	clear_tv(STACK_TV(idx));

    // Clear local variables and temp values, but not the return value.
    for (idx = ectx->ec_frame_idx + STACK_FRAME_SIZE;
					idx < ectx->ec_stack.ga_len - 1; ++idx)
	clear_tv(STACK_TV(idx));

    // Restore the previous frame.
    ectx->ec_dfunc_idx = STACK_TV(ectx->ec_frame_idx)->vval.v_number;
    ectx->ec_iidx = STACK_TV(ectx->ec_frame_idx + 1)->vval.v_number;
    ectx->ec_frame_idx = STACK_TV(ectx->ec_frame_idx + 2)->vval.v_number;
    dfunc = ((dfunc_T *)def_functions.ga_data) + ectx->ec_dfunc_idx;
    ectx->ec_instr = dfunc->df_instr;

    // Reset the stack to the position before the call, move the return value
    // to the top of the stack.
    idx = ectx->ec_stack.ga_len - 1;
    ectx->ec_stack.ga_len = top + 1;
    *STACK_TV_BOT(-1) = *STACK_TV(idx);

    return OK;
}

#undef STACK_TV

/*
 * Prepare arguments and rettv for calling a builtin or user function.
 */
    static int
call_prepare(int argcount, typval_T *argvars, ectx_T *ectx)
{
    int		idx;
    typval_T	*tv;

    // Move arguments from bottom of the stack to argvars[] and add terminator.
    for (idx = 0; idx < argcount; ++idx)
	argvars[idx] = *STACK_TV_BOT(idx - argcount);
    argvars[argcount].v_type = VAR_UNKNOWN;

    // Result replaces the arguments on the stack.
    if (argcount > 0)
	ectx->ec_stack.ga_len -= argcount - 1;
    else if (GA_GROW(&ectx->ec_stack, 1) == FAIL)
	return FAIL;
    else
	++ectx->ec_stack.ga_len;

    // Default return value is zero.
    tv = STACK_TV_BOT(-1);
    tv->v_type = VAR_NUMBER;
    tv->vval.v_number = 0;

    return OK;
}

// Ugly global to avoid passing the execution context around through many
// layers.
static ectx_T *current_ectx = NULL;

/*
 * Call a builtin function by index.
 */
    static int
call_bfunc(int func_idx, int argcount, ectx_T *ectx)
{
    typval_T	argvars[MAX_FUNC_ARGS];
    int		idx;
    int		did_emsg_before = did_emsg;
    ectx_T	*prev_ectx = current_ectx;

    if (call_prepare(argcount, argvars, ectx) == FAIL)
	return FAIL;

    // Call the builtin function.  Set "current_ectx" so that when it
    // recursively invokes call_def_function() a closure context can be set.
    current_ectx = ectx;
    call_internal_func_by_idx(func_idx, argvars, STACK_TV_BOT(-1));
    current_ectx = prev_ectx;

    // Clear the arguments.
    for (idx = 0; idx < argcount; ++idx)
	clear_tv(&argvars[idx]);

    if (did_emsg != did_emsg_before)
	return FAIL;
    return OK;
}

/*
 * Execute a user defined function.
 * "iptr" can be used to replace the instruction with a more efficient one.
 */
    static int
call_ufunc(ufunc_T *ufunc, int argcount, ectx_T *ectx, isn_T *iptr)
{
    typval_T	argvars[MAX_FUNC_ARGS];
    funcexe_T   funcexe;
    int		error;
    int		idx;
    int		called_emsg_before = called_emsg;

    if (ufunc->uf_def_status == UF_TO_BE_COMPILED
	    && compile_def_function(ufunc, FALSE, NULL) == FAIL)
	return FAIL;
    if (ufunc->uf_def_status == UF_COMPILED)
    {
	// The function has been compiled, can call it quickly.  For a function
	// that was defined later: we can call it directly next time.
	if (iptr != NULL)
	{
	    delete_instr(iptr);
	    iptr->isn_type = ISN_DCALL;
	    iptr->isn_arg.dfunc.cdf_idx = ufunc->uf_dfunc_idx;
	    iptr->isn_arg.dfunc.cdf_argcount = argcount;
	}
	return call_dfunc(ufunc->uf_dfunc_idx, argcount, ectx);
    }

    if (call_prepare(argcount, argvars, ectx) == FAIL)
	return FAIL;
    CLEAR_FIELD(funcexe);
    funcexe.evaluate = TRUE;

    // Call the user function.  Result goes in last position on the stack.
    // TODO: add selfdict if there is one
    error = call_user_func_check(ufunc, argcount, argvars,
					     STACK_TV_BOT(-1), &funcexe, NULL);

    // Clear the arguments.
    for (idx = 0; idx < argcount; ++idx)
	clear_tv(&argvars[idx]);

    if (error != FCERR_NONE)
    {
	user_func_error(error, ufunc->uf_name);
	return FAIL;
    }
    if (called_emsg > called_emsg_before)
	// Error other than from calling the function itself.
	return FAIL;
    return OK;
}

/*
 * Return TRUE if an error was given or CTRL-C was pressed.
 */
    static int
vim9_aborting(int prev_called_emsg)
{
    return called_emsg > prev_called_emsg || got_int || did_throw;
}

/*
 * Execute a function by "name".
 * This can be a builtin function or a user function.
 * "iptr" can be used to replace the instruction with a more efficient one.
 * Returns FAIL if not found without an error message.
 */
    static int
call_by_name(char_u *name, int argcount, ectx_T *ectx, isn_T *iptr)
{
    ufunc_T *ufunc;

    if (builtin_function(name, -1))
    {
	int func_idx = find_internal_func(name);

	if (func_idx < 0)
	    return FAIL;
	if (check_internal_func(func_idx, argcount) < 0)
	    return FAIL;
	return call_bfunc(func_idx, argcount, ectx);
    }

    ufunc = find_func(name, FALSE, NULL);

    if (ufunc == NULL)
    {
	int called_emsg_before = called_emsg;

	if (script_autoload(name, TRUE))
	    // loaded a package, search for the function again
	    ufunc = find_func(name, FALSE, NULL);
	if (vim9_aborting(called_emsg_before))
	    return FAIL;  // bail out if loading the script caused an error
    }

    if (ufunc != NULL)
	return call_ufunc(ufunc, argcount, ectx, iptr);

    return FAIL;
}

    static int
call_partial(typval_T *tv, int argcount_arg, ectx_T *ectx)
{
    int		argcount = argcount_arg;
    char_u	*name = NULL;
    int		called_emsg_before = called_emsg;

    if (tv->v_type == VAR_PARTIAL)
    {
	partial_T   *pt = tv->vval.v_partial;
	int	    i;

	if (pt->pt_argc > 0)
	{
	    // Make space for arguments from the partial, shift the "argcount"
	    // arguments up.
	    if (ga_grow(&ectx->ec_stack, pt->pt_argc) == FAIL)
		return FAIL;
	    for (i = 1; i <= argcount; ++i)
		*STACK_TV_BOT(-i + pt->pt_argc) = *STACK_TV_BOT(-i);
	    ectx->ec_stack.ga_len += pt->pt_argc;
	    argcount += pt->pt_argc;

	    // copy the arguments from the partial onto the stack
	    for (i = 0; i < pt->pt_argc; ++i)
		copy_tv(&pt->pt_argv[i], STACK_TV_BOT(-argcount + i));
	}

	if (pt->pt_func != NULL)
	{
	    int ret = call_ufunc(pt->pt_func, argcount, ectx, NULL);

	    // closure may need the function context where it was defined
	    ectx->ec_outer_stack = pt->pt_ectx_stack;
	    ectx->ec_outer_frame = pt->pt_ectx_frame;

	    return ret;
	}
	name = pt->pt_name;
    }
    else if (tv->v_type == VAR_FUNC)
	name = tv->vval.v_string;
    if (name == NULL || call_by_name(name, argcount, ectx, NULL) == FAIL)
    {
	if (called_emsg == called_emsg_before)
	    semsg(_(e_unknownfunc),
				  name == NULL ? (char_u *)"[unknown]" : name);
	return FAIL;
    }
    return OK;
}

/*
 * Store "tv" in variable "name".
 * This is for s: and g: variables.
 */
    static void
store_var(char_u *name, typval_T *tv)
{
    funccal_entry_T entry;

    save_funccal(&entry);
    set_var_const(name, NULL, tv, FALSE, LET_NO_COMMAND);
    restore_funccal();
}


/*
 * Execute a function by "name".
 * This can be a builtin function, user function or a funcref.
 * "iptr" can be used to replace the instruction with a more efficient one.
 */
    static int
call_eval_func(char_u *name, int argcount, ectx_T *ectx, isn_T *iptr)
{
    int	    called_emsg_before = called_emsg;
    int	    res;

    res = call_by_name(name, argcount, ectx, iptr);
    if (res == FAIL && called_emsg == called_emsg_before)
    {
	dictitem_T	*v;

	v = find_var(name, NULL, FALSE);
	if (v == NULL)
	{
	    semsg(_(e_unknownfunc), name);
	    return FAIL;
	}
	if (v->di_tv.v_type != VAR_PARTIAL && v->di_tv.v_type != VAR_FUNC)
	{
	    semsg(_(e_unknownfunc), name);
	    return FAIL;
	}
	return call_partial(&v->di_tv, argcount, ectx);
    }
    return res;
}

/*
 * Call a "def" function from old Vim script.
 * Return OK or FAIL.
 */
    int
call_def_function(
    ufunc_T	*ufunc,
    int		argc_arg,	// nr of arguments
    typval_T	*argv,		// arguments
    partial_T	*partial,	// optional partial for context
    typval_T	*rettv)		// return value
{
    ectx_T	ectx;		// execution context
    int		argc = argc_arg;
    int		initial_frame_idx;
    typval_T	*tv;
    int		idx;
    int		ret = FAIL;
    int		defcount = ufunc->uf_args.ga_len - argc;
    int		save_sc_version = current_sctx.sc_version;
    int		breakcheck_count = 0;
    int		called_emsg_before = called_emsg;

// Get pointer to item in the stack.
#define STACK_TV(idx) (((typval_T *)ectx.ec_stack.ga_data) + idx)

// Get pointer to item at the bottom of the stack, -1 is the bottom.
#undef STACK_TV_BOT
#define STACK_TV_BOT(idx) (((typval_T *)ectx.ec_stack.ga_data) + ectx.ec_stack.ga_len + idx)

// Get pointer to a local variable on the stack.  Negative for arguments.
#define STACK_TV_VAR(idx) (((typval_T *)ectx.ec_stack.ga_data) + ectx.ec_frame_idx + STACK_FRAME_SIZE + idx)

// Like STACK_TV_VAR but use the outer scope
#define STACK_OUT_TV_VAR(idx) (((typval_T *)ectx.ec_outer_stack->ga_data) + ectx.ec_outer_frame + STACK_FRAME_SIZE + idx)

    if (ufunc->uf_def_status == UF_NOT_COMPILED
	    || (ufunc->uf_def_status == UF_TO_BE_COMPILED
			  && compile_def_function(ufunc, FALSE, NULL) == FAIL))
    {
	if (called_emsg == called_emsg_before)
	    semsg(_("E1091: Function is not compiled: %s"),
						   printable_func_name(ufunc));
	return FAIL;
    }

    {
	// Check the function was really compiled.
	dfunc_T	*dfunc = ((dfunc_T *)def_functions.ga_data)
							 + ufunc->uf_dfunc_idx;
	if (dfunc->df_instr == NULL)
	{
	    iemsg("using call_def_function() on not compiled function");
	    return FAIL;
	}
    }

    CLEAR_FIELD(ectx);
    ectx.ec_dfunc_idx = ufunc->uf_dfunc_idx;
    ga_init2(&ectx.ec_stack, sizeof(typval_T), 500);
    if (ga_grow(&ectx.ec_stack, 20) == FAIL)
	return FAIL;
    ga_init2(&ectx.ec_trystack, sizeof(trycmd_T), 10);

    // Put arguments on the stack.
    for (idx = 0; idx < argc; ++idx)
    {
	if (ufunc->uf_arg_types != NULL && idx < ufunc->uf_args.ga_len
		&& check_typval_type(ufunc->uf_arg_types[idx], &argv[idx])
								       == FAIL)
	    goto failed_early;
	copy_tv(&argv[idx], STACK_TV_BOT(0));
	++ectx.ec_stack.ga_len;
    }

    // Turn varargs into a list.  Empty list if no args.
    if (ufunc->uf_va_name != NULL)
    {
	int vararg_count = argc - ufunc->uf_args.ga_len;

	if (vararg_count < 0)
	    vararg_count = 0;
	else
	    argc -= vararg_count;
	if (exe_newlist(vararg_count, &ectx) == FAIL)
	    goto failed_early;

	// Check the type of the list items.
	tv = STACK_TV_BOT(-1);
	if (ufunc->uf_va_type != NULL
		&& ufunc->uf_va_type->tt_member != &t_any
		&& tv->vval.v_list != NULL)
	{
	    type_T	*expected = ufunc->uf_va_type->tt_member;
	    listitem_T	*li = tv->vval.v_list->lv_first;

	    for (idx = 0; idx < vararg_count; ++idx)
	    {
		if (check_typval_type(expected, &li->li_tv) == FAIL)
		    goto failed_early;
		li = li->li_next;
	    }
	}

	if (defcount > 0)
	    // Move varargs list to below missing default arguments.
	    *STACK_TV_BOT(defcount - 1) = *STACK_TV_BOT(-1);
	--ectx.ec_stack.ga_len;
    }

    // Make space for omitted arguments, will store default value below.
    // Any varargs list goes after them.
    if (defcount > 0)
	for (idx = 0; idx < defcount; ++idx)
	{
	    STACK_TV_BOT(0)->v_type = VAR_UNKNOWN;
	    ++ectx.ec_stack.ga_len;
	}
    if (ufunc->uf_va_name != NULL)
	    ++ectx.ec_stack.ga_len;

    // Frame pointer points to just after arguments.
    ectx.ec_frame_idx = ectx.ec_stack.ga_len;
    initial_frame_idx = ectx.ec_frame_idx;

    if (partial != NULL)
    {
	if (partial->pt_ectx_stack == NULL && current_ectx != NULL)
	{
	    // TODO: is this always the right way?
	    ectx.ec_outer_stack = &current_ectx->ec_stack;
	    ectx.ec_outer_frame = current_ectx->ec_frame_idx;
	}
	else
	{
	    ectx.ec_outer_stack = partial->pt_ectx_stack;
	    ectx.ec_outer_frame = partial->pt_ectx_frame;
	}
    }

    // dummy frame entries
    for (idx = 0; idx < STACK_FRAME_SIZE; ++idx)
    {
	STACK_TV(ectx.ec_stack.ga_len)->v_type = VAR_UNKNOWN;
	++ectx.ec_stack.ga_len;
    }

    {
	// Reserve space for local variables and closure references.
	dfunc_T	*dfunc = ((dfunc_T *)def_functions.ga_data)
							 + ufunc->uf_dfunc_idx;
	int	count = dfunc->df_varcount + dfunc->df_closure_count;

	for (idx = 0; idx < count; ++idx)
	    STACK_TV_VAR(idx)->v_type = VAR_UNKNOWN;
	ectx.ec_stack.ga_len += count;

	ectx.ec_instr = dfunc->df_instr;
    }

    // Commands behave like vim9script.
    current_sctx.sc_version = SCRIPT_VERSION_VIM9;

    // Decide where to start execution, handles optional arguments.
    init_instr_idx(ufunc, argc, &ectx);

    for (;;)
    {
	isn_T	    *iptr;

	if (++breakcheck_count >= 100)
	{
	    line_breakcheck();
	    breakcheck_count = 0;
	}
	if (got_int)
	{
	    // Turn CTRL-C into an exception.
	    got_int = FALSE;
	    if (throw_exception("Vim:Interrupt", ET_INTERRUPT, NULL) == FAIL)
		goto failed;
	    did_throw = TRUE;
	}

	if (did_emsg && msg_list != NULL && *msg_list != NULL)
	{
	    // Turn an error message into an exception.
	    did_emsg = FALSE;
	    if (throw_exception(*msg_list, ET_ERROR, NULL) == FAIL)
		goto failed;
	    did_throw = TRUE;
	    *msg_list = NULL;
	}

	if (did_throw && !ectx.ec_in_catch)
	{
	    garray_T	*trystack = &ectx.ec_trystack;
	    trycmd_T    *trycmd = NULL;

	    // An exception jumps to the first catch, finally, or returns from
	    // the current function.
	    if (trystack->ga_len > 0)
		trycmd = ((trycmd_T *)trystack->ga_data) + trystack->ga_len - 1;
	    if (trycmd != NULL && trycmd->tcd_frame_idx == ectx.ec_frame_idx)
	    {
		// jump to ":catch" or ":finally"
		ectx.ec_in_catch = TRUE;
		ectx.ec_iidx = trycmd->tcd_catch_idx;
	    }
	    else
	    {
		// Not inside try or need to return from current functions.
		// Push a dummy return value.
		if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
		    goto failed;
		tv = STACK_TV_BOT(0);
		tv->v_type = VAR_NUMBER;
		tv->vval.v_number = 0;
		++ectx.ec_stack.ga_len;
		if (ectx.ec_frame_idx == initial_frame_idx)
		{
		    // At the toplevel we are done.
		    need_rethrow = TRUE;
		    if (handle_closure_in_use(&ectx, FALSE) == FAIL)
			goto failed;
		    goto done;
		}

		if (func_return(&ectx) == FAIL)
		    goto failed;
	    }
	    continue;
	}

	iptr = &ectx.ec_instr[ectx.ec_iidx++];
	switch (iptr->isn_type)
	{
	    // execute Ex command line
	    case ISN_EXEC:
		SOURCING_LNUM = iptr->isn_lnum;
		do_cmdline_cmd(iptr->isn_arg.string);
		break;

	    // execute Ex command from pieces on the stack
	    case ISN_EXECCONCAT:
		{
		    int	    count = iptr->isn_arg.number;
		    size_t  len = 0;
		    int	    pass;
		    int	    i;
		    char_u  *cmd = NULL;
		    char_u  *str;

		    for (pass = 1; pass <= 2; ++pass)
		    {
			for (i = 0; i < count; ++i)
			{
			    tv = STACK_TV_BOT(i - count);
			    str = tv->vval.v_string;
			    if (str != NULL && *str != NUL)
			    {
				if (pass == 2)
				    STRCPY(cmd + len, str);
				len += STRLEN(str);
			    }
			    if (pass == 2)
				clear_tv(tv);
			}
			if (pass == 1)
			{
			    cmd = alloc(len + 1);
			    if (cmd == NULL)
				goto failed;
			    len = 0;
			}
		    }

		    SOURCING_LNUM = iptr->isn_lnum;
		    do_cmdline_cmd(cmd);
		    vim_free(cmd);
		}
		break;

	    // execute :echo {string} ...
	    case ISN_ECHO:
		{
		    int count = iptr->isn_arg.echo.echo_count;
		    int	atstart = TRUE;
		    int needclr = TRUE;

		    for (idx = 0; idx < count; ++idx)
		    {
			tv = STACK_TV_BOT(idx - count);
			echo_one(tv, iptr->isn_arg.echo.echo_with_white,
							   &atstart, &needclr);
			clear_tv(tv);
		    }
		    if (needclr)
			msg_clr_eos();
		    ectx.ec_stack.ga_len -= count;
		}
		break;

	    // :execute {string} ...
	    // :echomsg {string} ...
	    // :echoerr {string} ...
	    case ISN_EXECUTE:
	    case ISN_ECHOMSG:
	    case ISN_ECHOERR:
		{
		    int		count = iptr->isn_arg.number;
		    garray_T	ga;
		    char_u	buf[NUMBUFLEN];
		    char_u	*p;
		    int		len;
		    int		failed = FALSE;

		    ga_init2(&ga, 1, 80);
		    for (idx = 0; idx < count; ++idx)
		    {
			tv = STACK_TV_BOT(idx - count);
			if (tv->v_type == VAR_CHANNEL || tv->v_type == VAR_JOB)
			{
			    SOURCING_LNUM = iptr->isn_lnum;
			    emsg(_(e_inval_string));
			    break;
			}
			else
			    p = tv_get_string_buf(tv, buf);

			len = (int)STRLEN(p);
			if (ga_grow(&ga, len + 2) == FAIL)
			    failed = TRUE;
			else
			{
			    if (ga.ga_len > 0)
				((char_u *)(ga.ga_data))[ga.ga_len++] = ' ';
			    STRCPY((char_u *)(ga.ga_data) + ga.ga_len, p);
			    ga.ga_len += len;
			}
			clear_tv(tv);
		    }
		    ectx.ec_stack.ga_len -= count;

		    if (!failed && ga.ga_data != NULL)
		    {
			if (iptr->isn_type == ISN_EXECUTE)
			    do_cmdline_cmd((char_u *)ga.ga_data);
			else
			{
			    msg_sb_eol();
			    if (iptr->isn_type == ISN_ECHOMSG)
			    {
				msg_attr(ga.ga_data, echo_attr);
				out_flush();
			    }
			    else
			    {
				SOURCING_LNUM = iptr->isn_lnum;
				emsg(ga.ga_data);
			    }
			}
		    }
		    ga_clear(&ga);
		}
		break;

	    // load local variable or argument
	    case ISN_LOAD:
		if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
		    goto failed;
		copy_tv(STACK_TV_VAR(iptr->isn_arg.number), STACK_TV_BOT(0));
		++ectx.ec_stack.ga_len;
		break;

	    // load variable or argument from outer scope
	    case ISN_LOADOUTER:
		if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
		    goto failed;
		copy_tv(STACK_OUT_TV_VAR(iptr->isn_arg.number),
							      STACK_TV_BOT(0));
		++ectx.ec_stack.ga_len;
		break;

	    // load v: variable
	    case ISN_LOADV:
		if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
		    goto failed;
		copy_tv(get_vim_var_tv(iptr->isn_arg.number), STACK_TV_BOT(0));
		++ectx.ec_stack.ga_len;
		break;

	    // load s: variable in Vim9 script
	    case ISN_LOADSCRIPT:
		{
		    scriptitem_T *si =
				  SCRIPT_ITEM(iptr->isn_arg.script.script_sid);
		    svar_T	 *sv;

		    sv = ((svar_T *)si->sn_var_vals.ga_data)
					     + iptr->isn_arg.script.script_idx;
		    if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
			goto failed;
		    copy_tv(sv->sv_tv, STACK_TV_BOT(0));
		    ++ectx.ec_stack.ga_len;
		}
		break;

	    // load s: variable in old script
	    case ISN_LOADS:
		{
		    hashtab_T	*ht = &SCRIPT_VARS(
					       iptr->isn_arg.loadstore.ls_sid);
		    char_u	*name = iptr->isn_arg.loadstore.ls_name;
		    dictitem_T	*di = find_var_in_ht(ht, 0, name, TRUE);

		    if (di == NULL)
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			semsg(_(e_undefvar), name);
			goto on_error;
		    }
		    else
		    {
			if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
			    goto failed;
			copy_tv(&di->di_tv, STACK_TV_BOT(0));
			++ectx.ec_stack.ga_len;
		    }
		}
		break;

	    // load g:/b:/w:/t: variable
	    case ISN_LOADG:
	    case ISN_LOADB:
	    case ISN_LOADW:
	    case ISN_LOADT:
		{
		    dictitem_T *di = NULL;
		    hashtab_T *ht = NULL;
		    char namespace;

		    switch (iptr->isn_type)
		    {
			case ISN_LOADG:
			    ht = get_globvar_ht();
			    namespace = 'g';
			    break;
			case ISN_LOADB:
			    ht = &curbuf->b_vars->dv_hashtab;
			    namespace = 'b';
			    break;
			case ISN_LOADW:
			    ht = &curwin->w_vars->dv_hashtab;
			    namespace = 'w';
			    break;
			case ISN_LOADT:
			    ht = &curtab->tp_vars->dv_hashtab;
			    namespace = 't';
			    break;
			default:  // Cannot reach here
			    goto failed;
		    }
		    di = find_var_in_ht(ht, 0, iptr->isn_arg.string, TRUE);

		    if (di == NULL)
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			semsg(_("E121: Undefined variable: %c:%s"),
					     namespace, iptr->isn_arg.string);
			goto on_error;
		    }
		    else
		    {
			if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
			    goto failed;
			copy_tv(&di->di_tv, STACK_TV_BOT(0));
			++ectx.ec_stack.ga_len;
		    }
		}
		break;

	    // load g:/b:/w:/t: namespace
	    case ISN_LOADGDICT:
	    case ISN_LOADBDICT:
	    case ISN_LOADWDICT:
	    case ISN_LOADTDICT:
		{
		    dict_T *d = NULL;

		    switch (iptr->isn_type)
		    {
			case ISN_LOADGDICT: d = get_globvar_dict(); break;
			case ISN_LOADBDICT: d = curbuf->b_vars; break;
			case ISN_LOADWDICT: d = curwin->w_vars; break;
			case ISN_LOADTDICT: d = curtab->tp_vars; break;
			default:  // Cannot reach here
			    goto failed;
		    }
		    if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
			goto failed;
		    tv = STACK_TV_BOT(0);
		    tv->v_type = VAR_DICT;
		    tv->v_lock = 0;
		    tv->vval.v_dict = d;
		    ++ectx.ec_stack.ga_len;
		}
		break;

	    // load &option
	    case ISN_LOADOPT:
		{
		    typval_T	optval;
		    char_u	*name = iptr->isn_arg.string;

		    // This is not expected to fail, name is checked during
		    // compilation: don't set SOURCING_LNUM.
		    if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
			goto failed;
		    if (eval_option(&name, &optval, TRUE) == FAIL)
			goto failed;
		    *STACK_TV_BOT(0) = optval;
		    ++ectx.ec_stack.ga_len;
		}
		break;

	    // load $ENV
	    case ISN_LOADENV:
		{
		    typval_T	optval;
		    char_u	*name = iptr->isn_arg.string;

		    if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
			goto failed;
		    // name is always valid, checked when compiling
		    (void)eval_env_var(&name, &optval, TRUE);
		    *STACK_TV_BOT(0) = optval;
		    ++ectx.ec_stack.ga_len;
		}
		break;

	    // load @register
	    case ISN_LOADREG:
		if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
		    goto failed;
		tv = STACK_TV_BOT(0);
		tv->v_type = VAR_STRING;
		tv->v_lock = 0;
		tv->vval.v_string = get_reg_contents(
					  iptr->isn_arg.number, GREG_EXPR_SRC);
		++ectx.ec_stack.ga_len;
		break;

	    // store local variable
	    case ISN_STORE:
		--ectx.ec_stack.ga_len;
		tv = STACK_TV_VAR(iptr->isn_arg.number);
		clear_tv(tv);
		*tv = *STACK_TV_BOT(0);
		break;

	    // store variable or argument in outer scope
	    case ISN_STOREOUTER:
		--ectx.ec_stack.ga_len;
		tv = STACK_OUT_TV_VAR(iptr->isn_arg.number);
		clear_tv(tv);
		*tv = *STACK_TV_BOT(0);
		break;

	    // store s: variable in old script
	    case ISN_STORES:
		{
		    hashtab_T	*ht = &SCRIPT_VARS(
					       iptr->isn_arg.loadstore.ls_sid);
		    char_u	*name = iptr->isn_arg.loadstore.ls_name;
		    dictitem_T	*di = find_var_in_ht(ht, 0, name + 2, TRUE);

		    --ectx.ec_stack.ga_len;
		    if (di == NULL)
			store_var(name, STACK_TV_BOT(0));
		    else
		    {
			clear_tv(&di->di_tv);
			di->di_tv = *STACK_TV_BOT(0);
		    }
		}
		break;

	    // store script-local variable in Vim9 script
	    case ISN_STORESCRIPT:
		{
		    scriptitem_T *si = SCRIPT_ITEM(
					      iptr->isn_arg.script.script_sid);
		    svar_T	 *sv = ((svar_T *)si->sn_var_vals.ga_data)
					     + iptr->isn_arg.script.script_idx;

		    --ectx.ec_stack.ga_len;
		    clear_tv(sv->sv_tv);
		    *sv->sv_tv = *STACK_TV_BOT(0);
		}
		break;

	    // store option
	    case ISN_STOREOPT:
		{
		    long	n = 0;
		    char_u	*s = NULL;
		    char	*msg;

		    --ectx.ec_stack.ga_len;
		    tv = STACK_TV_BOT(0);
		    if (tv->v_type == VAR_STRING)
		    {
			s = tv->vval.v_string;
			if (s == NULL)
			    s = (char_u *)"";
		    }
		    else
			// must be VAR_NUMBER, CHECKTYPE makes sure
			n = tv->vval.v_number;
		    msg = set_option_value(iptr->isn_arg.storeopt.so_name,
					n, s, iptr->isn_arg.storeopt.so_flags);
		    clear_tv(tv);
		    if (msg != NULL)
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			emsg(_(msg));
			goto on_error;
		    }
		}
		break;

	    // store $ENV
	    case ISN_STOREENV:
		--ectx.ec_stack.ga_len;
		tv = STACK_TV_BOT(0);
		vim_setenv_ext(iptr->isn_arg.string, tv_get_string(tv));
		clear_tv(tv);
		break;

	    // store @r
	    case ISN_STOREREG:
		{
		    int	reg = iptr->isn_arg.number;

		    --ectx.ec_stack.ga_len;
		    tv = STACK_TV_BOT(0);
		    write_reg_contents(reg == '@' ? '"' : reg,
						 tv_get_string(tv), -1, FALSE);
		    clear_tv(tv);
		}
		break;

	    // store v: variable
	    case ISN_STOREV:
		--ectx.ec_stack.ga_len;
		if (set_vim_var_tv(iptr->isn_arg.number, STACK_TV_BOT(0))
								       == FAIL)
		    // should not happen, type is checked when compiling
		    goto on_error;
		break;

	    // store g:/b:/w:/t: variable
	    case ISN_STOREG:
	    case ISN_STOREB:
	    case ISN_STOREW:
	    case ISN_STORET:
		{
		    dictitem_T *di;
		    hashtab_T *ht;
		    switch (iptr->isn_type)
		    {
			case ISN_STOREG:
			    ht = get_globvar_ht();
			    break;
			case ISN_STOREB:
			    ht = &curbuf->b_vars->dv_hashtab;
			    break;
			case ISN_STOREW:
			    ht = &curwin->w_vars->dv_hashtab;
			    break;
			case ISN_STORET:
			    ht = &curtab->tp_vars->dv_hashtab;
			    break;
			default:  // Cannot reach here
			    goto failed;
		    }

		    --ectx.ec_stack.ga_len;
		    di = find_var_in_ht(ht, 0, iptr->isn_arg.string + 2, TRUE);
		    if (di == NULL)
			store_var(iptr->isn_arg.string, STACK_TV_BOT(0));
		    else
		    {
			clear_tv(&di->di_tv);
			di->di_tv = *STACK_TV_BOT(0);
		    }
		}
		break;

	    // store number in local variable
	    case ISN_STORENR:
		tv = STACK_TV_VAR(iptr->isn_arg.storenr.stnr_idx);
		clear_tv(tv);
		tv->v_type = VAR_NUMBER;
		tv->vval.v_number = iptr->isn_arg.storenr.stnr_val;
		break;

	    // store value in list variable
	    case ISN_STORELIST:
		{
		    typval_T	*tv_idx = STACK_TV_BOT(-2);
		    varnumber_T	lidx = tv_idx->vval.v_number;
		    typval_T	*tv_list = STACK_TV_BOT(-1);
		    list_T	*list = tv_list->vval.v_list;

		    if (lidx < 0 && list->lv_len + lidx >= 0)
			// negative index is relative to the end
			lidx = list->lv_len + lidx;
		    if (lidx < 0 || lidx > list->lv_len)
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			semsg(_(e_listidx), lidx);
			goto on_error;
		    }
		    tv = STACK_TV_BOT(-3);
		    if (lidx < list->lv_len)
		    {
			listitem_T *li = list_find(list, lidx);

			// overwrite existing list item
			clear_tv(&li->li_tv);
			li->li_tv = *tv;
		    }
		    else
		    {
			// append to list, only fails when out of memory
			if (list_append_tv(list, tv) == FAIL)
			    goto failed;
			clear_tv(tv);
		    }
		    clear_tv(tv_idx);
		    clear_tv(tv_list);
		    ectx.ec_stack.ga_len -= 3;
		}
		break;

	    // store value in dict variable
	    case ISN_STOREDICT:
		{
		    typval_T	*tv_key = STACK_TV_BOT(-2);
		    char_u	*key = tv_key->vval.v_string;
		    typval_T	*tv_dict = STACK_TV_BOT(-1);
		    dict_T	*dict = tv_dict->vval.v_dict;
		    dictitem_T	*di;

		    if (dict == NULL)
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			emsg(_(e_dictionary_not_set));
			goto on_error;
		    }
		    if (key == NULL)
			key = (char_u *)"";
		    tv = STACK_TV_BOT(-3);
		    di = dict_find(dict, key, -1);
		    if (di != NULL)
		    {
			// overwrite existing value
			clear_tv(&di->di_tv);
			di->di_tv = *tv;
		    }
		    else
		    {
			// add to dict, only fails when out of memory
			if (dict_add_tv(dict, (char *)key, tv) == FAIL)
			    goto failed;
			clear_tv(tv);
		    }
		    clear_tv(tv_key);
		    clear_tv(tv_dict);
		    ectx.ec_stack.ga_len -= 3;
		}
		break;

	    // push constant
	    case ISN_PUSHNR:
	    case ISN_PUSHBOOL:
	    case ISN_PUSHSPEC:
	    case ISN_PUSHF:
	    case ISN_PUSHS:
	    case ISN_PUSHBLOB:
	    case ISN_PUSHFUNC:
	    case ISN_PUSHCHANNEL:
	    case ISN_PUSHJOB:
		if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
		    goto failed;
		tv = STACK_TV_BOT(0);
		tv->v_lock = 0;
		++ectx.ec_stack.ga_len;
		switch (iptr->isn_type)
		{
		    case ISN_PUSHNR:
			tv->v_type = VAR_NUMBER;
			tv->vval.v_number = iptr->isn_arg.number;
			break;
		    case ISN_PUSHBOOL:
			tv->v_type = VAR_BOOL;
			tv->vval.v_number = iptr->isn_arg.number;
			break;
		    case ISN_PUSHSPEC:
			tv->v_type = VAR_SPECIAL;
			tv->vval.v_number = iptr->isn_arg.number;
			break;
#ifdef FEAT_FLOAT
		    case ISN_PUSHF:
			tv->v_type = VAR_FLOAT;
			tv->vval.v_float = iptr->isn_arg.fnumber;
			break;
#endif
		    case ISN_PUSHBLOB:
			blob_copy(iptr->isn_arg.blob, tv);
			break;
		    case ISN_PUSHFUNC:
			tv->v_type = VAR_FUNC;
			if (iptr->isn_arg.string == NULL)
			    tv->vval.v_string = NULL;
			else
			    tv->vval.v_string =
					     vim_strsave(iptr->isn_arg.string);
			break;
		    case ISN_PUSHCHANNEL:
#ifdef FEAT_JOB_CHANNEL
			tv->v_type = VAR_CHANNEL;
			tv->vval.v_channel = iptr->isn_arg.channel;
			if (tv->vval.v_channel != NULL)
			    ++tv->vval.v_channel->ch_refcount;
#endif
			break;
		    case ISN_PUSHJOB:
#ifdef FEAT_JOB_CHANNEL
			tv->v_type = VAR_JOB;
			tv->vval.v_job = iptr->isn_arg.job;
			if (tv->vval.v_job != NULL)
			    ++tv->vval.v_job->jv_refcount;
#endif
			break;
		    default:
			tv->v_type = VAR_STRING;
			tv->vval.v_string = vim_strsave(
				iptr->isn_arg.string == NULL
					? (char_u *)"" : iptr->isn_arg.string);
		}
		break;

	    case ISN_UNLET:
		if (do_unlet(iptr->isn_arg.unlet.ul_name,
				       iptr->isn_arg.unlet.ul_forceit) == FAIL)
		    goto on_error;
		break;
	    case ISN_UNLETENV:
		vim_unsetenv(iptr->isn_arg.unlet.ul_name);
		break;

	    // create a list from items on the stack; uses a single allocation
	    // for the list header and the items
	    case ISN_NEWLIST:
		if (exe_newlist(iptr->isn_arg.number, &ectx) == FAIL)
		    goto failed;
		break;

	    // create a dict from items on the stack
	    case ISN_NEWDICT:
		{
		    int		count = iptr->isn_arg.number;
		    dict_T	*dict = dict_alloc();
		    dictitem_T	*item;

		    if (dict == NULL)
			goto failed;
		    for (idx = 0; idx < count; ++idx)
		    {
			// have already checked key type is VAR_STRING
			tv = STACK_TV_BOT(2 * (idx - count));
			// check key is unique
			item = dict_find(dict, tv->vval.v_string, -1);
			if (item != NULL)
			{
			    SOURCING_LNUM = iptr->isn_lnum;
			    semsg(_(e_duplicate_key), tv->vval.v_string);
			    dict_unref(dict);
			    goto on_error;
			}
			item = dictitem_alloc(tv->vval.v_string);
			clear_tv(tv);
			if (item == NULL)
			{
			    dict_unref(dict);
			    goto failed;
			}
			item->di_tv = *STACK_TV_BOT(2 * (idx - count) + 1);
			item->di_tv.v_lock = 0;
			if (dict_add(dict, item) == FAIL)
			{
			    // can this ever happen?
			    dict_unref(dict);
			    goto failed;
			}
		    }

		    if (count > 0)
			ectx.ec_stack.ga_len -= 2 * count - 1;
		    else if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
			goto failed;
		    else
			++ectx.ec_stack.ga_len;
		    tv = STACK_TV_BOT(-1);
		    tv->v_type = VAR_DICT;
		    tv->v_lock = 0;
		    tv->vval.v_dict = dict;
		    ++dict->dv_refcount;
		}
		break;

	    // call a :def function
	    case ISN_DCALL:
		if (call_dfunc(iptr->isn_arg.dfunc.cdf_idx,
			      iptr->isn_arg.dfunc.cdf_argcount,
			      &ectx) == FAIL)
		    goto on_error;
		break;

	    // call a builtin function
	    case ISN_BCALL:
		SOURCING_LNUM = iptr->isn_lnum;
		if (call_bfunc(iptr->isn_arg.bfunc.cbf_idx,
			      iptr->isn_arg.bfunc.cbf_argcount,
			      &ectx) == FAIL)
		    goto on_error;
		break;

	    // call a funcref or partial
	    case ISN_PCALL:
		{
		    cpfunc_T	*pfunc = &iptr->isn_arg.pfunc;
		    int		r;
		    typval_T	partial_tv;

		    SOURCING_LNUM = iptr->isn_lnum;
		    if (pfunc->cpf_top)
		    {
			// funcref is above the arguments
			tv = STACK_TV_BOT(-pfunc->cpf_argcount - 1);
		    }
		    else
		    {
			// Get the funcref from the stack.
			--ectx.ec_stack.ga_len;
			partial_tv = *STACK_TV_BOT(0);
			tv = &partial_tv;
		    }
		    r = call_partial(tv, pfunc->cpf_argcount, &ectx);
		    if (tv == &partial_tv)
			clear_tv(&partial_tv);
		    if (r == FAIL)
			goto on_error;
		}
		break;

	    case ISN_PCALL_END:
		// PCALL finished, arguments have been consumed and replaced by
		// the return value.  Now clear the funcref from the stack,
		// and move the return value in its place.
		--ectx.ec_stack.ga_len;
		clear_tv(STACK_TV_BOT(-1));
		*STACK_TV_BOT(-1) = *STACK_TV_BOT(0);
		break;

	    // call a user defined function or funcref/partial
	    case ISN_UCALL:
		{
		    cufunc_T	*cufunc = &iptr->isn_arg.ufunc;

		    SOURCING_LNUM = iptr->isn_lnum;
		    if (call_eval_func(cufunc->cuf_name,
				    cufunc->cuf_argcount, &ectx, iptr) == FAIL)
			goto on_error;
		}
		break;

	    // return from a :def function call
	    case ISN_RETURN:
		{
		    garray_T	*trystack = &ectx.ec_trystack;
		    trycmd_T    *trycmd = NULL;

		    if (trystack->ga_len > 0)
			trycmd = ((trycmd_T *)trystack->ga_data)
							+ trystack->ga_len - 1;
		    if (trycmd != NULL
				  && trycmd->tcd_frame_idx == ectx.ec_frame_idx
			    && trycmd->tcd_finally_idx != 0)
		    {
			// jump to ":finally"
			ectx.ec_iidx = trycmd->tcd_finally_idx;
			trycmd->tcd_return = TRUE;
		    }
		    else
			goto func_return;
		}
		break;

	    // push a function reference to a compiled function
	    case ISN_FUNCREF:
		{
		    partial_T   *pt = NULL;
		    dfunc_T	*pt_dfunc;

		    pt = ALLOC_CLEAR_ONE(partial_T);
		    if (pt == NULL)
			goto failed;
		    if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
		    {
			vim_free(pt);
			goto failed;
		    }
		    pt_dfunc = ((dfunc_T *)def_functions.ga_data)
					       + iptr->isn_arg.funcref.fr_func;
		    pt->pt_func = pt_dfunc->df_ufunc;
		    pt->pt_refcount = 1;
		    ++pt_dfunc->df_ufunc->uf_refcount;

		    if (pt_dfunc->df_ufunc->uf_flags & FC_CLOSURE)
		    {
			dfunc_T	*dfunc = ((dfunc_T *)def_functions.ga_data)
							   + ectx.ec_dfunc_idx;

			// The closure needs to find arguments and local
			// variables in the current stack.
			pt->pt_ectx_stack = &ectx.ec_stack;
			pt->pt_ectx_frame = ectx.ec_frame_idx;

			// If this function returns and the closure is still
			// used, we need to make a copy of the context
			// (arguments and local variables). Store a reference
			// to the partial so we can handle that.
			++pt->pt_refcount;
			tv = STACK_TV_VAR(dfunc->df_varcount
					   + iptr->isn_arg.funcref.fr_var_idx);
			if (tv->v_type == VAR_PARTIAL)
			{
			    // TODO: use a garray_T on ectx.
			    SOURCING_LNUM = iptr->isn_lnum;
			    emsg("Multiple closures not supported yet");
			    goto failed;
			}
			tv->v_type = VAR_PARTIAL;
			tv->vval.v_partial = pt;
		    }

		    tv = STACK_TV_BOT(0);
		    ++ectx.ec_stack.ga_len;
		    tv->vval.v_partial = pt;
		    tv->v_type = VAR_PARTIAL;
		    tv->v_lock = 0;
		}
		break;

	    // Create a global function from a lambda.
	    case ISN_NEWFUNC:
		{
		    newfunc_T	*newfunc = &iptr->isn_arg.newfunc;

		    copy_func(newfunc->nf_lambda, newfunc->nf_global);
		}
		break;

	    // jump if a condition is met
	    case ISN_JUMP:
		{
		    jumpwhen_T	when = iptr->isn_arg.jump.jump_when;
		    int		jump = TRUE;

		    if (when != JUMP_ALWAYS)
		    {
			tv = STACK_TV_BOT(-1);
			jump = tv2bool(tv);
			if (when == JUMP_IF_FALSE
					     || when == JUMP_AND_KEEP_IF_FALSE)
			    jump = !jump;
			if (when == JUMP_IF_FALSE || !jump)
			{
			    // drop the value from the stack
			    clear_tv(tv);
			    --ectx.ec_stack.ga_len;
			}
		    }
		    if (jump)
			ectx.ec_iidx = iptr->isn_arg.jump.jump_where;
		}
		break;

	    // top of a for loop
	    case ISN_FOR:
		{
		    list_T	*list = STACK_TV_BOT(-1)->vval.v_list;
		    typval_T	*idxtv =
				   STACK_TV_VAR(iptr->isn_arg.forloop.for_idx);

		    // push the next item from the list
		    if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
			goto failed;
		    if (++idxtv->vval.v_number >= list->lv_len)
			// past the end of the list, jump to "endfor"
			ectx.ec_iidx = iptr->isn_arg.forloop.for_end;
		    else if (list->lv_first == &range_list_item)
		    {
			// non-materialized range() list
			tv = STACK_TV_BOT(0);
			tv->v_type = VAR_NUMBER;
			tv->v_lock = 0;
			tv->vval.v_number = list_find_nr(
					     list, idxtv->vval.v_number, NULL);
			++ectx.ec_stack.ga_len;
		    }
		    else
		    {
			listitem_T *li = list_find(list, idxtv->vval.v_number);

			copy_tv(&li->li_tv, STACK_TV_BOT(0));
			++ectx.ec_stack.ga_len;
		    }
		}
		break;

	    // start of ":try" block
	    case ISN_TRY:
		{
		    trycmd_T    *trycmd = NULL;

		    if (GA_GROW(&ectx.ec_trystack, 1) == FAIL)
			goto failed;
		    trycmd = ((trycmd_T *)ectx.ec_trystack.ga_data)
						     + ectx.ec_trystack.ga_len;
		    ++ectx.ec_trystack.ga_len;
		    ++trylevel;
		    trycmd->tcd_frame_idx = ectx.ec_frame_idx;
		    trycmd->tcd_catch_idx = iptr->isn_arg.try.try_catch;
		    trycmd->tcd_finally_idx = iptr->isn_arg.try.try_finally;
		    trycmd->tcd_caught = FALSE;
		}
		break;

	    case ISN_PUSHEXC:
		if (current_exception == NULL)
		{
		    SOURCING_LNUM = iptr->isn_lnum;
		    iemsg("Evaluating catch while current_exception is NULL");
		    goto failed;
		}
		if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
		    goto failed;
		tv = STACK_TV_BOT(0);
		++ectx.ec_stack.ga_len;
		tv->v_type = VAR_STRING;
		tv->v_lock = 0;
		tv->vval.v_string = vim_strsave(
					   (char_u *)current_exception->value);
		break;

	    case ISN_CATCH:
		{
		    garray_T	*trystack = &ectx.ec_trystack;

		    if (trystack->ga_len > 0)
		    {
			trycmd_T    *trycmd = ((trycmd_T *)trystack->ga_data)
							+ trystack->ga_len - 1;
			trycmd->tcd_caught = TRUE;
		    }
		    did_emsg = got_int = did_throw = FALSE;
		    catch_exception(current_exception);
		}
		break;

	    // end of ":try" block
	    case ISN_ENDTRY:
		{
		    garray_T	*trystack = &ectx.ec_trystack;

		    if (trystack->ga_len > 0)
		    {
			trycmd_T    *trycmd = NULL;

			--trystack->ga_len;
			--trylevel;
			ectx.ec_in_catch = FALSE;
			trycmd = ((trycmd_T *)trystack->ga_data)
							    + trystack->ga_len;
			if (trycmd->tcd_caught && current_exception != NULL)
			{
			    // discard the exception
			    if (caught_stack == current_exception)
				caught_stack = caught_stack->caught;
			    discard_current_exception();
			}

			if (trycmd->tcd_return)
			    goto func_return;
		    }
		}
		break;

	    case ISN_THROW:
		--ectx.ec_stack.ga_len;
		tv = STACK_TV_BOT(0);
		if (throw_exception(tv->vval.v_string, ET_USER, NULL) == FAIL)
		{
		    vim_free(tv->vval.v_string);
		    goto failed;
		}
		did_throw = TRUE;
		break;

	    // compare with special values
	    case ISN_COMPAREBOOL:
	    case ISN_COMPARESPECIAL:
		{
		    typval_T	*tv1 = STACK_TV_BOT(-2);
		    typval_T	*tv2 = STACK_TV_BOT(-1);
		    varnumber_T arg1 = tv1->vval.v_number;
		    varnumber_T arg2 = tv2->vval.v_number;
		    int		res;

		    switch (iptr->isn_arg.op.op_type)
		    {
			case EXPR_EQUAL: res = arg1 == arg2; break;
			case EXPR_NEQUAL: res = arg1 != arg2; break;
			default: res = 0; break;
		    }

		    --ectx.ec_stack.ga_len;
		    tv1->v_type = VAR_BOOL;
		    tv1->vval.v_number = res ? VVAL_TRUE : VVAL_FALSE;
		}
		break;

	    // Operation with two number arguments
	    case ISN_OPNR:
	    case ISN_COMPARENR:
		{
		    typval_T	*tv1 = STACK_TV_BOT(-2);
		    typval_T	*tv2 = STACK_TV_BOT(-1);
		    varnumber_T arg1 = tv1->vval.v_number;
		    varnumber_T arg2 = tv2->vval.v_number;
		    varnumber_T res;

		    switch (iptr->isn_arg.op.op_type)
		    {
			case EXPR_MULT: res = arg1 * arg2; break;
			case EXPR_DIV: res = arg1 / arg2; break;
			case EXPR_REM: res = arg1 % arg2; break;
			case EXPR_SUB: res = arg1 - arg2; break;
			case EXPR_ADD: res = arg1 + arg2; break;

			case EXPR_EQUAL: res = arg1 == arg2; break;
			case EXPR_NEQUAL: res = arg1 != arg2; break;
			case EXPR_GREATER: res = arg1 > arg2; break;
			case EXPR_GEQUAL: res = arg1 >= arg2; break;
			case EXPR_SMALLER: res = arg1 < arg2; break;
			case EXPR_SEQUAL: res = arg1 <= arg2; break;
			default: res = 0; break;
		    }

		    --ectx.ec_stack.ga_len;
		    if (iptr->isn_type == ISN_COMPARENR)
		    {
			tv1->v_type = VAR_BOOL;
			tv1->vval.v_number = res ? VVAL_TRUE : VVAL_FALSE;
		    }
		    else
			tv1->vval.v_number = res;
		}
		break;

	    // Computation with two float arguments
	    case ISN_OPFLOAT:
	    case ISN_COMPAREFLOAT:
#ifdef FEAT_FLOAT
		{
		    typval_T	*tv1 = STACK_TV_BOT(-2);
		    typval_T	*tv2 = STACK_TV_BOT(-1);
		    float_T	arg1 = tv1->vval.v_float;
		    float_T	arg2 = tv2->vval.v_float;
		    float_T	res = 0;
		    int		cmp = FALSE;

		    switch (iptr->isn_arg.op.op_type)
		    {
			case EXPR_MULT: res = arg1 * arg2; break;
			case EXPR_DIV: res = arg1 / arg2; break;
			case EXPR_SUB: res = arg1 - arg2; break;
			case EXPR_ADD: res = arg1 + arg2; break;

			case EXPR_EQUAL: cmp = arg1 == arg2; break;
			case EXPR_NEQUAL: cmp = arg1 != arg2; break;
			case EXPR_GREATER: cmp = arg1 > arg2; break;
			case EXPR_GEQUAL: cmp = arg1 >= arg2; break;
			case EXPR_SMALLER: cmp = arg1 < arg2; break;
			case EXPR_SEQUAL: cmp = arg1 <= arg2; break;
			default: cmp = 0; break;
		    }
		    --ectx.ec_stack.ga_len;
		    if (iptr->isn_type == ISN_COMPAREFLOAT)
		    {
			tv1->v_type = VAR_BOOL;
			tv1->vval.v_number = cmp ? VVAL_TRUE : VVAL_FALSE;
		    }
		    else
			tv1->vval.v_float = res;
		}
#endif
		break;

	    case ISN_COMPARELIST:
		{
		    typval_T	*tv1 = STACK_TV_BOT(-2);
		    typval_T	*tv2 = STACK_TV_BOT(-1);
		    list_T	*arg1 = tv1->vval.v_list;
		    list_T	*arg2 = tv2->vval.v_list;
		    int		cmp = FALSE;
		    int		ic = iptr->isn_arg.op.op_ic;

		    switch (iptr->isn_arg.op.op_type)
		    {
			case EXPR_EQUAL: cmp =
				      list_equal(arg1, arg2, ic, FALSE); break;
			case EXPR_NEQUAL: cmp =
				     !list_equal(arg1, arg2, ic, FALSE); break;
			case EXPR_IS: cmp = arg1 == arg2; break;
			case EXPR_ISNOT: cmp = arg1 != arg2; break;
			default: cmp = 0; break;
		    }
		    --ectx.ec_stack.ga_len;
		    clear_tv(tv1);
		    clear_tv(tv2);
		    tv1->v_type = VAR_BOOL;
		    tv1->vval.v_number = cmp ? VVAL_TRUE : VVAL_FALSE;
		}
		break;

	    case ISN_COMPAREBLOB:
		{
		    typval_T	*tv1 = STACK_TV_BOT(-2);
		    typval_T	*tv2 = STACK_TV_BOT(-1);
		    blob_T	*arg1 = tv1->vval.v_blob;
		    blob_T	*arg2 = tv2->vval.v_blob;
		    int		cmp = FALSE;

		    switch (iptr->isn_arg.op.op_type)
		    {
			case EXPR_EQUAL: cmp = blob_equal(arg1, arg2); break;
			case EXPR_NEQUAL: cmp = !blob_equal(arg1, arg2); break;
			case EXPR_IS: cmp = arg1 == arg2; break;
			case EXPR_ISNOT: cmp = arg1 != arg2; break;
			default: cmp = 0; break;
		    }
		    --ectx.ec_stack.ga_len;
		    clear_tv(tv1);
		    clear_tv(tv2);
		    tv1->v_type = VAR_BOOL;
		    tv1->vval.v_number = cmp ? VVAL_TRUE : VVAL_FALSE;
		}
		break;

		// TODO: handle separately
	    case ISN_COMPARESTRING:
	    case ISN_COMPAREDICT:
	    case ISN_COMPAREFUNC:
	    case ISN_COMPAREANY:
		{
		    typval_T	*tv1 = STACK_TV_BOT(-2);
		    typval_T	*tv2 = STACK_TV_BOT(-1);
		    exptype_T	exptype = iptr->isn_arg.op.op_type;
		    int		ic = iptr->isn_arg.op.op_ic;

		    typval_compare(tv1, tv2, exptype, ic);
		    clear_tv(tv2);
		    --ectx.ec_stack.ga_len;
		}
		break;

	    case ISN_ADDLIST:
	    case ISN_ADDBLOB:
		{
		    typval_T *tv1 = STACK_TV_BOT(-2);
		    typval_T *tv2 = STACK_TV_BOT(-1);

		    if (iptr->isn_type == ISN_ADDLIST)
			eval_addlist(tv1, tv2);
		    else
			eval_addblob(tv1, tv2);
		    clear_tv(tv2);
		    --ectx.ec_stack.ga_len;
		}
		break;

	    // Computation with two arguments of unknown type
	    case ISN_OPANY:
		{
		    typval_T	*tv1 = STACK_TV_BOT(-2);
		    typval_T	*tv2 = STACK_TV_BOT(-1);
		    varnumber_T	n1, n2;
#ifdef FEAT_FLOAT
		    float_T	f1 = 0, f2 = 0;
#endif
		    int		error = FALSE;

		    if (iptr->isn_arg.op.op_type == EXPR_ADD)
		    {
			if (tv1->v_type == VAR_LIST && tv2->v_type == VAR_LIST)
			{
			    eval_addlist(tv1, tv2);
			    clear_tv(tv2);
			    --ectx.ec_stack.ga_len;
			    break;
			}
			else if (tv1->v_type == VAR_BLOB
						    && tv2->v_type == VAR_BLOB)
			{
			    eval_addblob(tv1, tv2);
			    clear_tv(tv2);
			    --ectx.ec_stack.ga_len;
			    break;
			}
		    }
#ifdef FEAT_FLOAT
		    if (tv1->v_type == VAR_FLOAT)
		    {
			f1 = tv1->vval.v_float;
			n1 = 0;
		    }
		    else
#endif
		    {
			n1 = tv_get_number_chk(tv1, &error);
			if (error)
			    goto on_error;
#ifdef FEAT_FLOAT
			if (tv2->v_type == VAR_FLOAT)
			    f1 = n1;
#endif
		    }
#ifdef FEAT_FLOAT
		    if (tv2->v_type == VAR_FLOAT)
		    {
			f2 = tv2->vval.v_float;
			n2 = 0;
		    }
		    else
#endif
		    {
			n2 = tv_get_number_chk(tv2, &error);
			if (error)
			    goto on_error;
#ifdef FEAT_FLOAT
			if (tv1->v_type == VAR_FLOAT)
			    f2 = n2;
#endif
		    }
#ifdef FEAT_FLOAT
		    // if there is a float on either side the result is a float
		    if (tv1->v_type == VAR_FLOAT || tv2->v_type == VAR_FLOAT)
		    {
			switch (iptr->isn_arg.op.op_type)
			{
			    case EXPR_MULT: f1 = f1 * f2; break;
			    case EXPR_DIV:  f1 = f1 / f2; break;
			    case EXPR_SUB:  f1 = f1 - f2; break;
			    case EXPR_ADD:  f1 = f1 + f2; break;
			    default: SOURCING_LNUM = iptr->isn_lnum;
				     emsg(_(e_modulus));
				     goto on_error;
			}
			clear_tv(tv1);
			clear_tv(tv2);
			tv1->v_type = VAR_FLOAT;
			tv1->vval.v_float = f1;
			--ectx.ec_stack.ga_len;
		    }
		    else
#endif
		    {
			switch (iptr->isn_arg.op.op_type)
			{
			    case EXPR_MULT: n1 = n1 * n2; break;
			    case EXPR_DIV:  n1 = num_divide(n1, n2); break;
			    case EXPR_SUB:  n1 = n1 - n2; break;
			    case EXPR_ADD:  n1 = n1 + n2; break;
			    default:	    n1 = num_modulus(n1, n2); break;
			}
			clear_tv(tv1);
			clear_tv(tv2);
			tv1->v_type = VAR_NUMBER;
			tv1->vval.v_number = n1;
			--ectx.ec_stack.ga_len;
		    }
		}
		break;

	    case ISN_CONCAT:
		{
		    char_u *str1 = STACK_TV_BOT(-2)->vval.v_string;
		    char_u *str2 = STACK_TV_BOT(-1)->vval.v_string;
		    char_u *res;

		    res = concat_str(str1, str2);
		    clear_tv(STACK_TV_BOT(-2));
		    clear_tv(STACK_TV_BOT(-1));
		    --ectx.ec_stack.ga_len;
		    STACK_TV_BOT(-1)->vval.v_string = res;
		}
		break;

	    case ISN_STRINDEX:
		{
		    char_u	*s;
		    varnumber_T	n;
		    char_u	*res;

		    // string index: string is at stack-2, index at stack-1
		    tv = STACK_TV_BOT(-2);
		    if (tv->v_type != VAR_STRING)
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			emsg(_(e_stringreq));
			goto on_error;
		    }
		    s = tv->vval.v_string;

		    tv = STACK_TV_BOT(-1);
		    if (tv->v_type != VAR_NUMBER)
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			emsg(_(e_number_exp));
			goto on_error;
		    }
		    n = tv->vval.v_number;

		    // The resulting variable is a string of a single
		    // character.  If the index is too big or negative the
		    // result is empty.
		    if (n < 0 || n >= (varnumber_T)STRLEN(s))
			res = NULL;
		    else
			res = vim_strnsave(s + n, 1);
		    --ectx.ec_stack.ga_len;
		    tv = STACK_TV_BOT(-1);
		    vim_free(tv->vval.v_string);
		    tv->vval.v_string = res;
		}
		break;

	    case ISN_LISTINDEX:
		{
		    list_T	*list;
		    varnumber_T	n;
		    listitem_T	*li;
		    typval_T	temp_tv;

		    // list index: list is at stack-2, index at stack-1
		    tv = STACK_TV_BOT(-2);
		    if (tv->v_type != VAR_LIST)
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			emsg(_(e_listreq));
			goto on_error;
		    }
		    list = tv->vval.v_list;

		    tv = STACK_TV_BOT(-1);
		    if (tv->v_type != VAR_NUMBER)
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			emsg(_(e_number_exp));
			goto on_error;
		    }
		    n = tv->vval.v_number;
		    clear_tv(tv);
		    if ((li = list_find(list, n)) == NULL)
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			semsg(_(e_listidx), n);
			goto on_error;
		    }
		    --ectx.ec_stack.ga_len;
		    // Clear the list after getting the item, to avoid that it
		    // makes the item invalid.
		    tv = STACK_TV_BOT(-1);
		    temp_tv = *tv;
		    copy_tv(&li->li_tv, tv);
		    clear_tv(&temp_tv);
		}
		break;

	    case ISN_SLICE:
		{
		    list_T	*list;
		    int		count = iptr->isn_arg.number;

		    // type will have been checked to be a list
		    tv = STACK_TV_BOT(-1);
		    list = tv->vval.v_list;

		    // no error for short list, expect it to be checked earlier
		    if (list != NULL && list->lv_len >= count)
		    {
			list_T	*newlist = list_slice(list,
						      count, list->lv_len - 1);

			if (newlist != NULL)
			{
			    list_unref(list);
			    tv->vval.v_list = newlist;
			    ++newlist->lv_refcount;
			}
		    }
		}
		break;

	    case ISN_GETITEM:
		{
		    listitem_T	*li;
		    int		index = iptr->isn_arg.number;

		    // Get list item: list is at stack-1, push item.
		    // List type and length is checked for when compiling.
		    tv = STACK_TV_BOT(-1);
		    li = list_find(tv->vval.v_list, index);

		    if (GA_GROW(&ectx.ec_stack, 1) == FAIL)
			goto failed;
		    ++ectx.ec_stack.ga_len;
		    copy_tv(&li->li_tv, STACK_TV_BOT(-1));
		}
		break;

	    case ISN_MEMBER:
		{
		    dict_T	*dict;
		    char_u	*key;
		    dictitem_T	*di;
		    typval_T	temp_tv;

		    // dict member: dict is at stack-2, key at stack-1
		    tv = STACK_TV_BOT(-2);
		    // no need to check for VAR_DICT, CHECKTYPE will check.
		    dict = tv->vval.v_dict;

		    tv = STACK_TV_BOT(-1);
		    // no need to check for VAR_STRING, 2STRING will check.
		    key = tv->vval.v_string;

		    if ((di = dict_find(dict, key, -1)) == NULL)
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			semsg(_(e_dictkey), key);
			goto on_error;
		    }
		    clear_tv(tv);
		    --ectx.ec_stack.ga_len;
		    // Clear the dict after getting the item, to avoid that it
		    // make the item invalid.
		    tv = STACK_TV_BOT(-1);
		    temp_tv = *tv;
		    copy_tv(&di->di_tv, tv);
		    clear_tv(&temp_tv);
		}
		break;

	    // dict member with string key
	    case ISN_STRINGMEMBER:
		{
		    dict_T	*dict;
		    dictitem_T	*di;
		    typval_T	temp_tv;

		    tv = STACK_TV_BOT(-1);
		    if (tv->v_type != VAR_DICT || tv->vval.v_dict == NULL)
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			emsg(_(e_dictreq));
			goto on_error;
		    }
		    dict = tv->vval.v_dict;

		    if ((di = dict_find(dict, iptr->isn_arg.string, -1))
								       == NULL)
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			semsg(_(e_dictkey), iptr->isn_arg.string);
			goto on_error;
		    }
		    // Clear the dict after getting the item, to avoid that it
		    // make the item invalid.
		    temp_tv = *tv;
		    copy_tv(&di->di_tv, tv);
		    clear_tv(&temp_tv);
		}
		break;

	    case ISN_NEGATENR:
		tv = STACK_TV_BOT(-1);
		if (tv->v_type != VAR_NUMBER
#ifdef FEAT_FLOAT
			&& tv->v_type != VAR_FLOAT
#endif
			)
		{
		    SOURCING_LNUM = iptr->isn_lnum;
		    emsg(_(e_number_exp));
		    goto on_error;
		}
#ifdef FEAT_FLOAT
		if (tv->v_type == VAR_FLOAT)
		    tv->vval.v_float = -tv->vval.v_float;
		else
#endif
		    tv->vval.v_number = -tv->vval.v_number;
		break;

	    case ISN_CHECKNR:
		{
		    int		error = FALSE;

		    tv = STACK_TV_BOT(-1);
		    if (check_not_string(tv) == FAIL)
			goto on_error;
		    (void)tv_get_number_chk(tv, &error);
		    if (error)
			goto on_error;
		}
		break;

	    case ISN_CHECKTYPE:
		{
		    checktype_T *ct = &iptr->isn_arg.type;

		    tv = STACK_TV_BOT(ct->ct_off);
		    // TODO: better type comparison
		    if (tv->v_type != ct->ct_type
			    && !((tv->v_type == VAR_PARTIAL
						   && ct->ct_type == VAR_FUNC)
				|| (tv->v_type == VAR_FUNC
					       && ct->ct_type == VAR_PARTIAL)))
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			semsg(_("E1029: Expected %s but got %s"),
				    vartype_name(ct->ct_type),
				    vartype_name(tv->v_type));
			goto on_error;
		    }
		}
		break;

	    case ISN_CHECKLEN:
		{
		    int	    min_len = iptr->isn_arg.checklen.cl_min_len;
		    list_T  *list = NULL;

		    tv = STACK_TV_BOT(-1);
		    if (tv->v_type == VAR_LIST)
			    list = tv->vval.v_list;
		    if (list == NULL || list->lv_len < min_len
			    || (list->lv_len > min_len
					&& !iptr->isn_arg.checklen.cl_more_OK))
		    {
			SOURCING_LNUM = iptr->isn_lnum;
			semsg(_("E1093: Expected %d items but got %d"),
				     min_len, list == NULL ? 0 : list->lv_len);
			goto on_error;
		    }
		}
		break;

	    case ISN_2BOOL:
		{
		    int n;

		    tv = STACK_TV_BOT(-1);
		    n = tv2bool(tv);
		    if (iptr->isn_arg.number)  // invert
			n = !n;
		    clear_tv(tv);
		    tv->v_type = VAR_BOOL;
		    tv->vval.v_number = n ? VVAL_TRUE : VVAL_FALSE;
		}
		break;

	    case ISN_2STRING:
	    case ISN_2STRING_ANY:
		{
		    char_u *str;

		    tv = STACK_TV_BOT(iptr->isn_arg.number);
		    if (tv->v_type != VAR_STRING)
		    {
			if (iptr->isn_type == ISN_2STRING_ANY)
			{
			    switch (tv->v_type)
			    {
				case VAR_SPECIAL:
				case VAR_BOOL:
				case VAR_NUMBER:
				case VAR_FLOAT:
				case VAR_BLOB:	break;
				default:	to_string_error(tv->v_type);
						goto on_error;
			    }
			}
			str = typval_tostring(tv);
			clear_tv(tv);
			tv->v_type = VAR_STRING;
			tv->vval.v_string = str;
		    }
		}
		break;

	    case ISN_SHUFFLE:
		{
		    typval_T	    tmp_tv;
		    int		    item = iptr->isn_arg.shuffle.shfl_item;
		    int		    up = iptr->isn_arg.shuffle.shfl_up;

		    tmp_tv = *STACK_TV_BOT(-item);
		    for ( ; up > 0 && item > 1; --up)
		    {
			*STACK_TV_BOT(-item) = *STACK_TV_BOT(-item + 1);
			--item;
		    }
		    *STACK_TV_BOT(-item) = tmp_tv;
		}
		break;

	    case ISN_DROP:
		--ectx.ec_stack.ga_len;
		clear_tv(STACK_TV_BOT(0));
		break;
	}
	continue;

func_return:
	// Restore previous function. If the frame pointer is zero then there
	// is none and we are done.
	if (ectx.ec_frame_idx == initial_frame_idx)
	{
	    if (handle_closure_in_use(&ectx, FALSE) == FAIL)
		// only fails when out of memory
		goto failed;
	    goto done;
	}
	if (func_return(&ectx) == FAIL)
	    // only fails when out of memory
	    goto failed;
	continue;

on_error:
	if (trylevel == 0)
	    goto failed;
    }

done:
    // function finished, get result from the stack.
    tv = STACK_TV_BOT(-1);
    *rettv = *tv;
    tv->v_type = VAR_UNKNOWN;
    ret = OK;

failed:
    // When failed need to unwind the call stack.
    while (ectx.ec_frame_idx != initial_frame_idx)
	func_return(&ectx);
failed_early:
    current_sctx.sc_version = save_sc_version;

    // Free all local variables, but not arguments.
    for (idx = 0; idx < ectx.ec_stack.ga_len; ++idx)
	clear_tv(STACK_TV(idx));

    vim_free(ectx.ec_stack.ga_data);
    vim_free(ectx.ec_trystack.ga_data);

    if (ret != OK && called_emsg == called_emsg_before)
	semsg(_("E1099: Unknown error while executing %s"),
						   printable_func_name(ufunc));
    return ret;
}

/*
 * ":dissassemble".
 * We don't really need this at runtime, but we do have tests that require it,
 * so always include this.
 */
    void
ex_disassemble(exarg_T *eap)
{
    char_u	*arg = eap->arg;
    char_u	*fname;
    ufunc_T	*ufunc;
    dfunc_T	*dfunc;
    isn_T	*instr;
    int		current;
    int		line_idx = 0;
    int		prev_current = 0;
    int		is_global = FALSE;

    if (STRNCMP(arg, "<lambda>", 8) == 0)
    {
	arg += 8;
	(void)getdigits(&arg);
	fname = vim_strnsave(eap->arg, arg - eap->arg);
    }
    else
	fname = trans_function_name(&arg, &is_global, FALSE,
			    TFN_INT | TFN_QUIET | TFN_NO_AUTOLOAD, NULL, NULL);
    if (fname == NULL)
    {
	semsg(_(e_invarg2), eap->arg);
	return;
    }

    ufunc = find_func(fname, is_global, NULL);
    if (ufunc == NULL)
    {
	char_u *p = untrans_function_name(fname);

	if (p != NULL)
	    // Try again without making it script-local.
	    ufunc = find_func(p, FALSE, NULL);
    }
    vim_free(fname);
    if (ufunc == NULL)
    {
	semsg(_("E1061: Cannot find function %s"), eap->arg);
	return;
    }
    if (ufunc->uf_def_status == UF_TO_BE_COMPILED
	    && compile_def_function(ufunc, FALSE, NULL) == FAIL)
	return;
    if (ufunc->uf_def_status != UF_COMPILED)
    {
	semsg(_("E1062: Function %s is not compiled"), eap->arg);
	return;
    }
    if (ufunc->uf_name_exp != NULL)
	msg((char *)ufunc->uf_name_exp);
    else
	msg((char *)ufunc->uf_name);

    dfunc = ((dfunc_T *)def_functions.ga_data) + ufunc->uf_dfunc_idx;
    instr = dfunc->df_instr;
    for (current = 0; current < dfunc->df_instr_count; ++current)
    {
	isn_T	    *iptr = &instr[current];
	char	    *line;

	while (line_idx < iptr->isn_lnum && line_idx < ufunc->uf_lines.ga_len)
	{
	    if (current > prev_current)
	    {
		msg_puts("\n\n");
		prev_current = current;
	    }
	    line = ((char **)ufunc->uf_lines.ga_data)[line_idx++];
	    if (line != NULL)
		msg(line);
	}

	switch (iptr->isn_type)
	{
	    case ISN_EXEC:
		smsg("%4d EXEC %s", current, iptr->isn_arg.string);
		break;
	    case ISN_EXECCONCAT:
		smsg("%4d EXECCONCAT %lld", current,
					      (long long)iptr->isn_arg.number);
		break;
	    case ISN_ECHO:
		{
		    echo_T *echo = &iptr->isn_arg.echo;

		    smsg("%4d %s %d", current,
			    echo->echo_with_white ? "ECHO" : "ECHON",
			    echo->echo_count);
		}
		break;
	    case ISN_EXECUTE:
		smsg("%4d EXECUTE %lld", current,
					    (long long)(iptr->isn_arg.number));
		break;
	    case ISN_ECHOMSG:
		smsg("%4d ECHOMSG %lld", current,
					    (long long)(iptr->isn_arg.number));
		break;
	    case ISN_ECHOERR:
		smsg("%4d ECHOERR %lld", current,
					    (long long)(iptr->isn_arg.number));
		break;
	    case ISN_LOAD:
	    case ISN_LOADOUTER:
		{
		    char *add = iptr->isn_type == ISN_LOAD ? "" : "OUTER";

		    if (iptr->isn_arg.number < 0)
			smsg("%4d LOAD%s arg[%lld]", current, add,
				(long long)(iptr->isn_arg.number
							  + STACK_FRAME_SIZE));
		    else
			smsg("%4d LOAD%s $%lld", current, add,
					    (long long)(iptr->isn_arg.number));
		}
		break;
	    case ISN_LOADV:
		smsg("%4d LOADV v:%s", current,
				       get_vim_var_name(iptr->isn_arg.number));
		break;
	    case ISN_LOADSCRIPT:
		{
		    scriptitem_T *si =
				  SCRIPT_ITEM(iptr->isn_arg.script.script_sid);
		    svar_T *sv = ((svar_T *)si->sn_var_vals.ga_data)
					     + iptr->isn_arg.script.script_idx;

		    smsg("%4d LOADSCRIPT %s from %s", current,
						     sv->sv_name, si->sn_name);
		}
		break;
	    case ISN_LOADS:
		{
		    scriptitem_T *si = SCRIPT_ITEM(
					       iptr->isn_arg.loadstore.ls_sid);

		    smsg("%4d LOADS s:%s from %s", current,
				 iptr->isn_arg.loadstore.ls_name, si->sn_name);
		}
		break;
	    case ISN_LOADG:
		smsg("%4d LOADG g:%s", current, iptr->isn_arg.string);
		break;
	    case ISN_LOADB:
		smsg("%4d LOADB b:%s", current, iptr->isn_arg.string);
		break;
	    case ISN_LOADW:
		smsg("%4d LOADW w:%s", current, iptr->isn_arg.string);
		break;
	    case ISN_LOADT:
		smsg("%4d LOADT t:%s", current, iptr->isn_arg.string);
		break;
	    case ISN_LOADGDICT:
		smsg("%4d LOAD g:", current);
		break;
	    case ISN_LOADBDICT:
		smsg("%4d LOAD b:", current);
		break;
	    case ISN_LOADWDICT:
		smsg("%4d LOAD w:", current);
		break;
	    case ISN_LOADTDICT:
		smsg("%4d LOAD t:", current);
		break;
	    case ISN_LOADOPT:
		smsg("%4d LOADOPT %s", current, iptr->isn_arg.string);
		break;
	    case ISN_LOADENV:
		smsg("%4d LOADENV %s", current, iptr->isn_arg.string);
		break;
	    case ISN_LOADREG:
		smsg("%4d LOADREG @%c", current, (int)(iptr->isn_arg.number));
		break;

	    case ISN_STORE:
	    case ISN_STOREOUTER:
		{
		    char *add = iptr->isn_type == ISN_STORE ? "" : "OUTER";

		if (iptr->isn_arg.number < 0)
		    smsg("%4d STORE%s arg[%lld]", current, add,
			 (long long)(iptr->isn_arg.number + STACK_FRAME_SIZE));
		else
		    smsg("%4d STORE%s $%lld", current, add,
					    (long long)(iptr->isn_arg.number));
		}
		break;
	    case ISN_STOREV:
		smsg("%4d STOREV v:%s", current,
				       get_vim_var_name(iptr->isn_arg.number));
		break;
	    case ISN_STOREG:
		smsg("%4d STOREG %s", current, iptr->isn_arg.string);
		break;
	    case ISN_STOREB:
		smsg("%4d STOREB %s", current, iptr->isn_arg.string);
		break;
	    case ISN_STOREW:
		smsg("%4d STOREW %s", current, iptr->isn_arg.string);
		break;
	    case ISN_STORET:
		smsg("%4d STORET %s", current, iptr->isn_arg.string);
		break;
	    case ISN_STORES:
		{
		    scriptitem_T *si = SCRIPT_ITEM(
					       iptr->isn_arg.loadstore.ls_sid);

		    smsg("%4d STORES %s in %s", current,
				 iptr->isn_arg.loadstore.ls_name, si->sn_name);
		}
		break;
	    case ISN_STORESCRIPT:
		{
		    scriptitem_T *si =
				  SCRIPT_ITEM(iptr->isn_arg.script.script_sid);
		    svar_T *sv = ((svar_T *)si->sn_var_vals.ga_data)
					     + iptr->isn_arg.script.script_idx;

		    smsg("%4d STORESCRIPT %s in %s", current,
						     sv->sv_name, si->sn_name);
		}
		break;
	    case ISN_STOREOPT:
		smsg("%4d STOREOPT &%s", current,
					       iptr->isn_arg.storeopt.so_name);
		break;
	    case ISN_STOREENV:
		smsg("%4d STOREENV $%s", current, iptr->isn_arg.string);
		break;
	    case ISN_STOREREG:
		smsg("%4d STOREREG @%c", current, (int)iptr->isn_arg.number);
		break;
	    case ISN_STORENR:
		smsg("%4d STORE %lld in $%d", current,
				iptr->isn_arg.storenr.stnr_val,
				iptr->isn_arg.storenr.stnr_idx);
		break;

	    case ISN_STORELIST:
		smsg("%4d STORELIST", current);
		break;

	    case ISN_STOREDICT:
		smsg("%4d STOREDICT", current);
		break;

	    // constants
	    case ISN_PUSHNR:
		smsg("%4d PUSHNR %lld", current,
					    (long long)(iptr->isn_arg.number));
		break;
	    case ISN_PUSHBOOL:
	    case ISN_PUSHSPEC:
		smsg("%4d PUSH %s", current,
				   get_var_special_name(iptr->isn_arg.number));
		break;
	    case ISN_PUSHF:
#ifdef FEAT_FLOAT
		smsg("%4d PUSHF %g", current, iptr->isn_arg.fnumber);
#endif
		break;
	    case ISN_PUSHS:
		smsg("%4d PUSHS \"%s\"", current, iptr->isn_arg.string);
		break;
	    case ISN_PUSHBLOB:
		{
		    char_u	*r;
		    char_u	numbuf[NUMBUFLEN];
		    char_u	*tofree;

		    r = blob2string(iptr->isn_arg.blob, &tofree, numbuf);
		    smsg("%4d PUSHBLOB %s", current, r);
		    vim_free(tofree);
		}
		break;
	    case ISN_PUSHFUNC:
		{
		    char *name = (char *)iptr->isn_arg.string;

		    smsg("%4d PUSHFUNC \"%s\"", current,
					       name == NULL ? "[none]" : name);
		}
		break;
	    case ISN_PUSHCHANNEL:
#ifdef FEAT_JOB_CHANNEL
		{
		    channel_T *channel = iptr->isn_arg.channel;

		    smsg("%4d PUSHCHANNEL %d", current,
					 channel == NULL ? 0 : channel->ch_id);
		}
#endif
		break;
	    case ISN_PUSHJOB:
#ifdef FEAT_JOB_CHANNEL
		{
		    typval_T	tv;
		    char_u	*name;

		    tv.v_type = VAR_JOB;
		    tv.vval.v_job = iptr->isn_arg.job;
		    name = tv_get_string(&tv);
		    smsg("%4d PUSHJOB \"%s\"", current, name);
		}
#endif
		break;
	    case ISN_PUSHEXC:
		smsg("%4d PUSH v:exception", current);
		break;
	    case ISN_UNLET:
		smsg("%4d UNLET%s %s", current,
			iptr->isn_arg.unlet.ul_forceit ? "!" : "",
			iptr->isn_arg.unlet.ul_name);
		break;
	    case ISN_UNLETENV:
		smsg("%4d UNLETENV%s $%s", current,
			iptr->isn_arg.unlet.ul_forceit ? "!" : "",
			iptr->isn_arg.unlet.ul_name);
		break;
	    case ISN_NEWLIST:
		smsg("%4d NEWLIST size %lld", current,
					    (long long)(iptr->isn_arg.number));
		break;
	    case ISN_NEWDICT:
		smsg("%4d NEWDICT size %lld", current,
					    (long long)(iptr->isn_arg.number));
		break;

	    // function call
	    case ISN_BCALL:
		{
		    cbfunc_T	*cbfunc = &iptr->isn_arg.bfunc;

		    smsg("%4d BCALL %s(argc %d)", current,
			    internal_func_name(cbfunc->cbf_idx),
			    cbfunc->cbf_argcount);
		}
		break;
	    case ISN_DCALL:
		{
		    cdfunc_T	*cdfunc = &iptr->isn_arg.dfunc;
		    dfunc_T	*df = ((dfunc_T *)def_functions.ga_data)
							     + cdfunc->cdf_idx;

		    smsg("%4d DCALL %s(argc %d)", current,
			    df->df_ufunc->uf_name_exp != NULL
				? df->df_ufunc->uf_name_exp
				: df->df_ufunc->uf_name, cdfunc->cdf_argcount);
		}
		break;
	    case ISN_UCALL:
		{
		    cufunc_T	*cufunc = &iptr->isn_arg.ufunc;

		    smsg("%4d UCALL %s(argc %d)", current,
				       cufunc->cuf_name, cufunc->cuf_argcount);
		}
		break;
	    case ISN_PCALL:
		{
		    cpfunc_T	*cpfunc = &iptr->isn_arg.pfunc;

		    smsg("%4d PCALL%s (argc %d)", current,
			   cpfunc->cpf_top ? " top" : "", cpfunc->cpf_argcount);
		}
		break;
	    case ISN_PCALL_END:
		smsg("%4d PCALL end", current);
		break;
	    case ISN_RETURN:
		smsg("%4d RETURN", current);
		break;
	    case ISN_FUNCREF:
		{
		    funcref_T	*funcref = &iptr->isn_arg.funcref;
		    dfunc_T	*df = ((dfunc_T *)def_functions.ga_data)
							    + funcref->fr_func;

		    smsg("%4d FUNCREF %s $%d", current, df->df_ufunc->uf_name,
				     funcref->fr_var_idx + dfunc->df_varcount);
		}
		break;

	    case ISN_NEWFUNC:
		{
		    newfunc_T	*newfunc = &iptr->isn_arg.newfunc;

		    smsg("%4d NEWFUNC %s %s", current,
				       newfunc->nf_lambda, newfunc->nf_global);
		}
		break;

	    case ISN_JUMP:
		{
		    char *when = "?";

		    switch (iptr->isn_arg.jump.jump_when)
		    {
			case JUMP_ALWAYS:
			    when = "JUMP";
			    break;
			case JUMP_AND_KEEP_IF_TRUE:
			    when = "JUMP_AND_KEEP_IF_TRUE";
			    break;
			case JUMP_IF_FALSE:
			    when = "JUMP_IF_FALSE";
			    break;
			case JUMP_AND_KEEP_IF_FALSE:
			    when = "JUMP_AND_KEEP_IF_FALSE";
			    break;
		    }
		    smsg("%4d %s -> %d", current, when,
						iptr->isn_arg.jump.jump_where);
		}
		break;

	    case ISN_FOR:
		{
		    forloop_T *forloop = &iptr->isn_arg.forloop;

		    smsg("%4d FOR $%d -> %d", current,
					   forloop->for_idx, forloop->for_end);
		}
		break;

	    case ISN_TRY:
		{
		    try_T *try = &iptr->isn_arg.try;

		    smsg("%4d TRY catch -> %d, finally -> %d", current,
					     try->try_catch, try->try_finally);
		}
		break;
	    case ISN_CATCH:
		// TODO
		smsg("%4d CATCH", current);
		break;
	    case ISN_ENDTRY:
		smsg("%4d ENDTRY", current);
		break;
	    case ISN_THROW:
		smsg("%4d THROW", current);
		break;

	    // expression operations on number
	    case ISN_OPNR:
	    case ISN_OPFLOAT:
	    case ISN_OPANY:
		{
		    char *what;
		    char *ins;

		    switch (iptr->isn_arg.op.op_type)
		    {
			case EXPR_MULT: what = "*"; break;
			case EXPR_DIV: what = "/"; break;
			case EXPR_REM: what = "%"; break;
			case EXPR_SUB: what = "-"; break;
			case EXPR_ADD: what = "+"; break;
			default:       what = "???"; break;
		    }
		    switch (iptr->isn_type)
		    {
			case ISN_OPNR: ins = "OPNR"; break;
			case ISN_OPFLOAT: ins = "OPFLOAT"; break;
			case ISN_OPANY: ins = "OPANY"; break;
			default: ins = "???"; break;
		    }
		    smsg("%4d %s %s", current, ins, what);
		}
		break;

	    case ISN_COMPAREBOOL:
	    case ISN_COMPARESPECIAL:
	    case ISN_COMPARENR:
	    case ISN_COMPAREFLOAT:
	    case ISN_COMPARESTRING:
	    case ISN_COMPAREBLOB:
	    case ISN_COMPARELIST:
	    case ISN_COMPAREDICT:
	    case ISN_COMPAREFUNC:
	    case ISN_COMPAREANY:
		   {
		       char *p;
		       char buf[10];
		       char *type;

		       switch (iptr->isn_arg.op.op_type)
		       {
			   case EXPR_EQUAL:	 p = "=="; break;
			   case EXPR_NEQUAL:    p = "!="; break;
			   case EXPR_GREATER:   p = ">"; break;
			   case EXPR_GEQUAL:    p = ">="; break;
			   case EXPR_SMALLER:   p = "<"; break;
			   case EXPR_SEQUAL:    p = "<="; break;
			   case EXPR_MATCH:	 p = "=~"; break;
			   case EXPR_IS:	 p = "is"; break;
			   case EXPR_ISNOT:	 p = "isnot"; break;
			   case EXPR_NOMATCH:	 p = "!~"; break;
			   default:  p = "???"; break;
		       }
		       STRCPY(buf, p);
		       if (iptr->isn_arg.op.op_ic == TRUE)
			   strcat(buf, "?");
		       switch(iptr->isn_type)
		       {
			   case ISN_COMPAREBOOL: type = "COMPAREBOOL"; break;
			   case ISN_COMPARESPECIAL:
						 type = "COMPARESPECIAL"; break;
			   case ISN_COMPARENR: type = "COMPARENR"; break;
			   case ISN_COMPAREFLOAT: type = "COMPAREFLOAT"; break;
			   case ISN_COMPARESTRING:
						  type = "COMPARESTRING"; break;
			   case ISN_COMPAREBLOB: type = "COMPAREBLOB"; break;
			   case ISN_COMPARELIST: type = "COMPARELIST"; break;
			   case ISN_COMPAREDICT: type = "COMPAREDICT"; break;
			   case ISN_COMPAREFUNC: type = "COMPAREFUNC"; break;
			   case ISN_COMPAREANY: type = "COMPAREANY"; break;
			   default: type = "???"; break;
		       }

		       smsg("%4d %s %s", current, type, buf);
		   }
		   break;

	    case ISN_ADDLIST: smsg("%4d ADDLIST", current); break;
	    case ISN_ADDBLOB: smsg("%4d ADDBLOB", current); break;

	    // expression operations
	    case ISN_CONCAT: smsg("%4d CONCAT", current); break;
	    case ISN_STRINDEX: smsg("%4d STRINDEX", current); break;
	    case ISN_LISTINDEX: smsg("%4d LISTINDEX", current); break;
	    case ISN_SLICE: smsg("%4d SLICE %lld",
					 current, iptr->isn_arg.number); break;
	    case ISN_GETITEM: smsg("%4d ITEM %lld",
					 current, iptr->isn_arg.number); break;
	    case ISN_MEMBER: smsg("%4d MEMBER", current); break;
	    case ISN_STRINGMEMBER: smsg("%4d MEMBER %s", current,
						  iptr->isn_arg.string); break;
	    case ISN_NEGATENR: smsg("%4d NEGATENR", current); break;

	    case ISN_CHECKNR: smsg("%4d CHECKNR", current); break;
	    case ISN_CHECKTYPE: smsg("%4d CHECKTYPE %s stack[%d]", current,
				      vartype_name(iptr->isn_arg.type.ct_type),
				      iptr->isn_arg.type.ct_off);
				break;
	    case ISN_CHECKLEN: smsg("%4d CHECKLEN %s%d", current,
				iptr->isn_arg.checklen.cl_more_OK ? ">= " : "",
				iptr->isn_arg.checklen.cl_min_len);
			       break;
	    case ISN_2BOOL: if (iptr->isn_arg.number)
				smsg("%4d INVERT (!val)", current);
			    else
				smsg("%4d 2BOOL (!!val)", current);
			    break;
	    case ISN_2STRING: smsg("%4d 2STRING stack[%lld]", current,
					 (long long)(iptr->isn_arg.number));
			      break;
	    case ISN_2STRING_ANY: smsg("%4d 2STRING_ANY stack[%lld]", current,
					 (long long)(iptr->isn_arg.number));
			      break;

	    case ISN_SHUFFLE: smsg("%4d SHUFFLE %d up %d", current,
					 iptr->isn_arg.shuffle.shfl_item,
					 iptr->isn_arg.shuffle.shfl_up);
			      break;
	    case ISN_DROP: smsg("%4d DROP", current); break;
	}
    }
}

/*
 * Return TRUE when "tv" is not falsey: non-zero, non-empty string, non-empty
 * list, etc.  Mostly like what JavaScript does, except that empty list and
 * empty dictionary are FALSE.
 */
    int
tv2bool(typval_T *tv)
{
    switch (tv->v_type)
    {
	case VAR_NUMBER:
	    return tv->vval.v_number != 0;
	case VAR_FLOAT:
#ifdef FEAT_FLOAT
	    return tv->vval.v_float != 0.0;
#else
	    break;
#endif
	case VAR_PARTIAL:
	    return tv->vval.v_partial != NULL;
	case VAR_FUNC:
	case VAR_STRING:
	    return tv->vval.v_string != NULL && *tv->vval.v_string != NUL;
	case VAR_LIST:
	    return tv->vval.v_list != NULL && tv->vval.v_list->lv_len > 0;
	case VAR_DICT:
	    return tv->vval.v_dict != NULL
				    && tv->vval.v_dict->dv_hashtab.ht_used > 0;
	case VAR_BOOL:
	case VAR_SPECIAL:
	    return tv->vval.v_number == VVAL_TRUE ? TRUE : FALSE;
	case VAR_JOB:
#ifdef FEAT_JOB_CHANNEL
	    return tv->vval.v_job != NULL;
#else
	    break;
#endif
	case VAR_CHANNEL:
#ifdef FEAT_JOB_CHANNEL
	    return tv->vval.v_channel != NULL;
#else
	    break;
#endif
	case VAR_BLOB:
	    return tv->vval.v_blob != NULL && tv->vval.v_blob->bv_ga.ga_len > 0;
	case VAR_UNKNOWN:
	case VAR_ANY:
	case VAR_VOID:
	    break;
    }
    return FALSE;
}

/*
 * If "tv" is a string give an error and return FAIL.
 */
    int
check_not_string(typval_T *tv)
{
    if (tv->v_type == VAR_STRING)
    {
	emsg(_("E1030: Using a String as a Number"));
	clear_tv(tv);
	return FAIL;
    }
    return OK;
}


#endif // FEAT_EVAL
