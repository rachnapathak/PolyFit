// $Id$
# ifndef CPPAD_REV_JAC_SWEEP_HPP
# define CPPAD_REV_JAC_SWEEP_HPP

/* --------------------------------------------------------------------------
CppAD: C++ Algorithmic Differentiation: Copyright (C) 2003-15 Bradley M. Bell

CppAD is distributed under multiple licenses. This distribution is under
the terms of the
                    Eclipse Public License Version 1.0.

A copy of this license is included in the COPYING file of this distribution.
Please visit http://www.coin-or.org/CppAD/ for information on other licenses.
-------------------------------------------------------------------------- */

namespace CppAD { // BEGIN_CPPAD_NAMESPACE
/*!
\file rev_jac_sweep.hpp
Compute Reverse mode Jacobian sparsity patterns.
*/

/*!
\def CPPAD_REV_JAC_SWEEP_TRACE
This value is either zero or one.
Zero is the normal operational value.
If it is one, a trace of every rev_jac_sweep computation is printed.
*/
# define CPPAD_REV_JAC_SWEEP_TRACE 0

/*
\def CPPAD_ATOMIC_CALL
This avoids warnings when NDEBUG is defined and user_ok is not used.
If \c NDEBUG is defined, this resolves to
\code
	user_atom->rev_sparse_jac
\endcode
otherwise, it respolves to
\code
	user_ok = user_atom->rev_sparse_jac
\endcode
This maco is undefined at the end of this file to facillitate is
use with a different definition in other files.
*/
# ifdef NDEBUG
# define CPPAD_ATOMIC_CALL user_atom->rev_sparse_jac
# else
# define CPPAD_ATOMIC_CALL user_ok = user_atom->rev_sparse_jac
# endif

/*!
Given the sparsity pattern for the dependent variables,
RevJacSweep computes the sparsity pattern for all the independent variables.

\tparam Base
base type for the operator; i.e., this operation sequence was recorded
using AD< \a Base > and computations by this routine are done using type
\a Base.

\tparam Vector_set
is the type used for vectors of sets. It can be either
\c sparse_pack, \c sparse_set, or \c sparse_list.

\param dependency
Are the derivatives with respect to left and right of the expression below
considered to be non-zero:
\code
	CondExpRel(left, right, if_true, if_false)
\endcode
This is used by the optimizer to obtain the correct dependency relations.

\param n
is the number of independent variables on the tape.

\param numvar
is the total number of variables on the tape; i.e.,
\a play->num_var_rec().
This is also the number of rows in the entire sparsity pattern \a RevJac.

\param play
The information stored in \a play
is a recording of the operations corresponding to a function
\f[
	F : {\bf R}^n \rightarrow {\bf R}^m
\f]
where \f$ n \f$ is the number of independent variables
and \f$ m \f$ is the number of dependent variables.
The object \a play is effectly constant.
It is not declared const because while playing back the tape
the object \a play holds information about the currentl location
with in the tape and this changes during playback.

\param var_sparsity
For i = 0 , ... , \a numvar - 1,
(all the variables on the tape)
the forward Jacobian sparsity pattern for variable i
corresponds to the set with index i in \a var_sparsity.
\b
\b
\b Input:
For i = 0 , ... , \a numvar - 1,
the forward Jacobian sparsity pattern for variable i is an input
if i corresponds to a dependent variable.
Otherwise the sparsity patten is empty.
\n
\n
\b Output: For j = 1 , ... , \a n,
the sparsity pattern for the dependent variable with index (j-1)
is given by the set with index index j in \a var_sparsity.
*/

template <class Base, class Vector_set>
void RevJacSweep(
	bool                  dependency,
	size_t                n,
	size_t                numvar,
	player<Base>         *play,
	Vector_set&           var_sparsity
)
{
	OpCode           op;
	size_t         i_op;
	size_t        i_var;

	const addr_t*   arg = CPPAD_NULL;

	size_t            i, j, k;

	// length of the parameter vector (used by CppAD assert macros)
	const size_t num_par = play->num_par_rec();

	// check numvar argument
	CPPAD_ASSERT_UNKNOWN( numvar > 0 );
	CPPAD_ASSERT_UNKNOWN( play->num_var_rec()   == numvar );
	CPPAD_ASSERT_UNKNOWN( var_sparsity.n_set() == numvar );

	// upper limit (exclusive) for elements in the set
	size_t limit = var_sparsity.end();

	// vecad_sparsity contains a sparsity pattern for each VecAD object.
	// vecad_ind maps a VecAD index (beginning of the VecAD object)
	// to the index of the corresponding set in vecad_sparsity.
	size_t num_vecad_ind   = play->num_vec_ind_rec();
	size_t num_vecad_vec   = play->num_vecad_vec_rec();
	Vector_set  vecad_sparsity;
	vecad_sparsity.resize(num_vecad_vec, limit);
	pod_vector<size_t> vecad_ind;
	if( num_vecad_vec > 0 )
	{	size_t length;
		vecad_ind.extend(num_vecad_ind);
		j             = 0;
		for(i = 0; i < num_vecad_vec; i++)
		{	// length of this VecAD
			length   = play->GetVecInd(j);
			// set to proper index for this VecAD
			vecad_ind[j] = i;
			for(k = 1; k <= length; k++)
				vecad_ind[j+k] = num_vecad_vec; // invalid index
			// start of next VecAD
			j       += length + 1;
		}
		CPPAD_ASSERT_UNKNOWN( j == play->num_vec_ind_rec() );
	}

	// work space used by UserOp.
	typedef std::set<size_t> size_set;
	size_set::iterator set_itr;  // iterator for a standard set
	size_set::iterator set_end;  // end of iterator sequence
	vector< size_set > set_r;   // set sparsity pattern for the argument x
	vector< size_set > set_s;   // set sparisty pattern for the result y
	//
	vector<bool>       bool_r;   // bool sparsity pattern for the argument x
	vector<bool>       bool_s;   // bool sparisty pattern for the result y
	//
	vectorBool         pack_r;   // pack sparsity pattern for the argument x
	vectorBool         pack_s;   // pack sparisty pattern for the result y
	//
	const size_t user_q = limit; // maximum element plus one
	size_t user_index = 0;       // indentifier for this atomic operation
	size_t user_id    = 0;       // user identifier for this call to operator
	size_t user_i     = 0;       // index in result vector
	size_t user_j     = 0;       // index in argument vector
	size_t user_m     = 0;       // size of result vector
	size_t user_n     = 0;       // size of arugment vector
	//
	atomic_base<Base>* user_atom = CPPAD_NULL; // user's atomic op calculator
	bool               user_pack = false;      // sparsity pattern type is pack
	bool               user_bool = false;      // sparsity pattern type is bool
	bool               user_set  = false;      // sparsity pattern type is set
# ifndef NDEBUG
	bool               user_ok   = false;      // atomic op return value
# endif
	// next expected operator in a UserOp sequence
	enum { user_start, user_arg, user_ret, user_end } user_state = user_end;

	// Initialize
	play->reverse_start(op, arg, i_op, i_var);
	CPPAD_ASSERT_UNKNOWN( op == EndOp );
# if CPPAD_REV_JAC_SWEEP_TRACE
	std::cout << std::endl;
	CppAD::vectorBool z_value(limit);
# endif
	bool more_operators = true;
	while(more_operators)
	{
		// next op
		play->reverse_next(op, arg, i_op, i_var);
# ifndef NDEBUG
		if( i_op <= n )
		{	CPPAD_ASSERT_UNKNOWN((op == InvOp) | (op == BeginOp));
		}
		else	CPPAD_ASSERT_UNKNOWN((op != InvOp) & (op != BeginOp));
# endif

		// rest of information depends on the case
		switch( op )
		{
			case AbsOp:
			CPPAD_ASSERT_NARG_NRES(op, 1, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

			case AddvvOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			reverse_sparse_jacobian_binary_op(
				i_var, arg, var_sparsity
			);
			break;
			// -------------------------------------------------

			case AddpvOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[1], var_sparsity
			);
			break;
			// -------------------------------------------------

			case AcosOp:
			// sqrt(1 - x * x), acos(x)
			CPPAD_ASSERT_NARG_NRES(op, 1, 2);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

# if CPPAD_USE_CPLUSPLUS_2011
			case AcoshOp:
			// sqrt(x * x - 1), acosh(x)
			CPPAD_ASSERT_NARG_NRES(op, 1, 2);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
# endif
			// -------------------------------------------------

			case AsinOp:
			// sqrt(1 - x * x), asin(x)
			CPPAD_ASSERT_NARG_NRES(op, 1, 2);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

# if CPPAD_USE_CPLUSPLUS_2011
			case AsinhOp:
			// sqrt(1 + x * x), asinh(x)
			CPPAD_ASSERT_NARG_NRES(op, 1, 2);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
# endif
			// -------------------------------------------------

			case AtanOp:
			// 1 + x * x, atan(x)
			CPPAD_ASSERT_NARG_NRES(op, 1, 2);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

# if CPPAD_USE_CPLUSPLUS_2011
			case AtanhOp:
			// 1 - x * x, atanh(x)
			CPPAD_ASSERT_NARG_NRES(op, 1, 2);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
# endif
			// -------------------------------------------------

			case BeginOp:
			CPPAD_ASSERT_NARG_NRES(op, 1, 1);
			more_operators = false;
			break;
			// -------------------------------------------------

			case CSkipOp:
			// CSkipOp has a variable number of arguments and
			// reverse_next thinks it one has one argument.
			// We must inform reverse_next of this special case.
			play->reverse_cskip(op, arg, i_op, i_var);
			break;
			// -------------------------------------------------

			case CSumOp:
			// CSumOp has a variable number of arguments and
			// reverse_next thinks it one has one argument.
			// We must inform reverse_next of this special case.
			play->reverse_csum(op, arg, i_op, i_var);
			reverse_sparse_jacobian_csum_op(
				i_var, arg, var_sparsity
			);
			break;
			// -------------------------------------------------

			case CExpOp:
			reverse_sparse_jacobian_cond_op(
				dependency, i_var, arg, num_par, var_sparsity
			);
			break;
			// ---------------------------------------------------

			case CosOp:
			// sin(x), cos(x)
			CPPAD_ASSERT_NARG_NRES(op, 1, 2);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// ---------------------------------------------------

			case CoshOp:
			// sinh(x), cosh(x)
			CPPAD_ASSERT_NARG_NRES(op, 1, 2);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

			case DisOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			// derivative is identically zero but dependency is not
			if( dependency ) reverse_sparse_jacobian_unary_op(
				i_var, arg[1], var_sparsity
			);
			break;
			// -------------------------------------------------

			case DivvvOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			reverse_sparse_jacobian_binary_op(
				i_var, arg, var_sparsity
			);
			break;
			// -------------------------------------------------

			case DivpvOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[1], var_sparsity
			);
			break;
			// -------------------------------------------------

			case DivvpOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

			case ErfOp:
			// arg[1] is always the parameter 0
			// arg[0] is always the parameter 2 / sqrt(pi)
			CPPAD_ASSERT_NARG_NRES(op, 3, 5);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

			case ExpOp:
			CPPAD_ASSERT_NARG_NRES(op, 1, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

# if CPPAD_USE_CPLUSPLUS_2011
			case Expm1Op:
			CPPAD_ASSERT_NARG_NRES(op, 1, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
# endif
			// -------------------------------------------------

			case InvOp:
			CPPAD_ASSERT_NARG_NRES(op, 0, 1);
			break;
			// -------------------------------------------------

			case LdpOp:
			reverse_sparse_jacobian_load_op(
				dependency,
				op,
				i_var,
				arg,
				num_vecad_ind,
				vecad_ind.data(),
				var_sparsity,
				vecad_sparsity
			);
			break;
			// -------------------------------------------------

			case LdvOp:
			reverse_sparse_jacobian_load_op(
				dependency,
				op,
				i_var,
				arg,
				num_vecad_ind,
				vecad_ind.data(),
				var_sparsity,
				vecad_sparsity
			);
			break;
			// -------------------------------------------------

			case EqpvOp:
			case EqvvOp:
			case LtpvOp:
			case LtvpOp:
			case LtvvOp:
			case LepvOp:
			case LevpOp:
			case LevvOp:
			case NepvOp:
			case NevvOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 0);
			break;
			// -------------------------------------------------

			case LogOp:
			CPPAD_ASSERT_NARG_NRES(op, 1, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

# if CPPAD_USE_CPLUSPLUS_2011
			case Log1pOp:
			CPPAD_ASSERT_NARG_NRES(op, 1, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
# endif
			// -------------------------------------------------

			case MulpvOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[1], var_sparsity
			);
			break;
			// -------------------------------------------------

			case MulvvOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			reverse_sparse_jacobian_binary_op(
				i_var, arg, var_sparsity
			);
			break;
			// -------------------------------------------------

			case ParOp:
			CPPAD_ASSERT_NARG_NRES(op, 1, 1);

			break;
			// -------------------------------------------------

			case PowvpOp:
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

			case PowpvOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 3);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[1], var_sparsity
			);
			break;
			// -------------------------------------------------

			case PowvvOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 3);
			reverse_sparse_jacobian_binary_op(
				i_var, arg, var_sparsity
			);
			break;
			// -------------------------------------------------

			case PriOp:
			CPPAD_ASSERT_NARG_NRES(op, 5, 0);
			break;
			// -------------------------------------------------

			case SignOp:
			CPPAD_ASSERT_NARG_NRES(op, 1, 1);
			// derivative is identically zero but dependency is not
			if( dependency ) reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

			case SinOp:
			// cos(x), sin(x)
			CPPAD_ASSERT_NARG_NRES(op, 1, 2);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

			case SinhOp:
			// cosh(x), sinh(x)
			CPPAD_ASSERT_NARG_NRES(op, 1, 2);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

			case SqrtOp:
			CPPAD_ASSERT_NARG_NRES(op, 1, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

			case StppOp:
			// does not affect sparsity or dependency when both are parameters
			CPPAD_ASSERT_NARG_NRES(op, 3, 0);
			break;
			// -------------------------------------------------

			case StpvOp:
			reverse_sparse_jacobian_store_op(
				dependency,
				op,
				arg,
				num_vecad_ind,
				vecad_ind.data(),
				var_sparsity,
				vecad_sparsity
			);
			break;
			// -------------------------------------------------

			case StvpOp:
			CPPAD_ASSERT_NARG_NRES(op, 3, 0);
			// storing a parameter only affects dependency
			reverse_sparse_jacobian_store_op(
				dependency,
				op,
				arg,
				num_vecad_ind,
				vecad_ind.data(),
				var_sparsity,
				vecad_sparsity
			);
			break;
			// -------------------------------------------------

			case StvvOp:
			reverse_sparse_jacobian_store_op(
				dependency,
				op,
				arg,
				num_vecad_ind,
				vecad_ind.data(),
				var_sparsity,
				vecad_sparsity
			);
			break;
			// -------------------------------------------------

			case SubvvOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			reverse_sparse_jacobian_binary_op(
				i_var, arg, var_sparsity
			);
			break;
			// -------------------------------------------------

			case SubpvOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[1], var_sparsity
			);
			break;
			// -------------------------------------------------

			case SubvpOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

			case TanOp:
			// tan(x)^2, tan(x)
			CPPAD_ASSERT_NARG_NRES(op, 1, 2);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

			case TanhOp:
			// tanh(x)^2, tanh(x)
			CPPAD_ASSERT_NARG_NRES(op, 1, 2);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

			case UserOp:
			// start or end atomic operation sequence
			CPPAD_ASSERT_UNKNOWN( NumRes( UserOp ) == 0 );
			CPPAD_ASSERT_UNKNOWN( NumArg( UserOp ) == 4 );
			if( user_state == user_end )
			{	user_index = arg[0];
				user_id    = arg[1];
				user_n     = arg[2];
				user_m     = arg[3];
				user_atom  = atomic_base<Base>::class_object(user_index);
# ifndef NDEBUG
				if( user_atom == CPPAD_NULL )
				{	std::string msg =
						atomic_base<Base>::class_name(user_index)
						+ ": atomic_base function has been deleted";
					CPPAD_ASSERT_KNOWN(false, msg.c_str() );
				}
# endif
				user_pack  = user_atom->sparsity() ==
							atomic_base<Base>::pack_sparsity_enum;
				user_bool  = user_atom->sparsity() ==
							atomic_base<Base>::bool_sparsity_enum;
				user_set   = user_atom->sparsity() ==
							atomic_base<Base>::set_sparsity_enum;
				CPPAD_ASSERT_UNKNOWN( user_pack || user_bool || user_set );
				if( user_pack )
				{	if( pack_r.size() != user_m * user_q )
						pack_r.resize( user_m * user_q );
					if( pack_s.size() != user_n * user_q )
						pack_s.resize( user_n * user_q );
					for(i = 0; i < user_m; i++)
						for(j = 0; j < user_q; j++)
							pack_r[ i * user_q + j] = false;
				}
				if( user_bool )
				{	if( bool_r.size() != user_m * user_q )
						bool_r.resize( user_m * user_q );
					if( bool_s.size() != user_n * user_q )
						bool_s.resize( user_n * user_q );
					for(i = 0; i < user_m; i++)
						for(j = 0; j < user_q; j++)
							bool_r[ i * user_q + j] = false;
				}
				if( user_set )
				{	if(set_r.size() != user_m )
						set_r.resize(user_m);
					if(set_s.size() != user_n )
						set_s.resize(user_n);
					for(i = 0; i < user_m; i++)
						set_r[i].clear();
				}
				user_j     = user_n;
				user_i     = user_m;
				user_state = user_ret;
			}
			else
			{	CPPAD_ASSERT_UNKNOWN( user_state == user_start );
				CPPAD_ASSERT_UNKNOWN( user_index == size_t(arg[0]) );
				CPPAD_ASSERT_UNKNOWN( user_id    == size_t(arg[1]) );
				CPPAD_ASSERT_UNKNOWN( user_n     == size_t(arg[2]) );
				CPPAD_ASSERT_UNKNOWN( user_m     == size_t(arg[3]) );
# ifndef NDEBUG
				if( ! user_ok )
				{	std::string msg =
						atomic_base<Base>::class_name(user_index)
						+ ": atomic_base.rev_sparse_jac: returned false\n";
					if( user_pack )
						msg += "sparsity = pack_sparsity_enum";
					if( user_bool )
						msg += "sparsity = bool_sparsity_enum";
					if( user_set )
						msg += "sparsity = set_sparsity_enum";
					CPPAD_ASSERT_KNOWN(false, msg.c_str() );
				}
# endif
				user_state = user_end;
               }
			break;

			case UsrapOp:
			// parameter argument in an atomic operation sequence
			CPPAD_ASSERT_UNKNOWN( user_state == user_arg );
			CPPAD_ASSERT_UNKNOWN( 0 < user_j && user_j <= user_n );
			CPPAD_ASSERT_UNKNOWN( NumArg(op) == 1 );
			CPPAD_ASSERT_UNKNOWN( size_t(arg[0]) < num_par );
			--user_j;
			if( user_j == 0 )
				user_state = user_start;
			break;

			case UsravOp:
			// variable argument in an atomic operation sequence
			CPPAD_ASSERT_UNKNOWN( user_state == user_arg );
			CPPAD_ASSERT_UNKNOWN( 0 < user_j && user_j <= user_n );
			CPPAD_ASSERT_UNKNOWN( NumArg(op) == 1 );
			CPPAD_ASSERT_UNKNOWN( size_t(arg[0]) <= i_var );
			CPPAD_ASSERT_UNKNOWN( 0 < arg[0] );
			--user_j;
			// 2DO: It might be faster if we add set union to var_sparsity
			// where one of the sets is not in var_sparsity.
			if( user_pack )
			{	for(j = 0; j < user_q; j++)
					if( pack_s[ user_j * user_q + j ] )
						var_sparsity.add_element(arg[0], j);
			}
			if( user_bool )
			{	for(j = 0; j < user_q; j++)
					if( bool_s[ user_j * user_q + j ] )
						var_sparsity.add_element(arg[0], j);
			}
			if( user_set )
			{	set_itr = set_s[user_j].begin();
				set_end = set_s[user_j].end();
				while( set_itr != set_end )
					var_sparsity.add_element(arg[0], *set_itr++);
			}
			if( user_j == 0 )
				user_state = user_start;
			break;

			case UsrrpOp:
			// parameter result in an atomic operation sequence
			CPPAD_ASSERT_UNKNOWN( user_state == user_ret );
			CPPAD_ASSERT_UNKNOWN( 0 < user_i && user_i <= user_m );
			CPPAD_ASSERT_UNKNOWN( NumArg(op) == 1 );
			CPPAD_ASSERT_UNKNOWN( size_t(arg[0]) < num_par );
			--user_i;
			if( user_i == 0 )
			{	// call users function for this operation
				user_atom->set_id(user_id);
				if( user_pack )
					CPPAD_ATOMIC_CALL( user_q, pack_r, pack_s);
				if( user_bool )
					CPPAD_ATOMIC_CALL( user_q, bool_r, bool_s);
				if( user_set )
					CPPAD_ATOMIC_CALL( user_q, set_r, set_s);
				user_state = user_arg;
			}
			break;

			case UsrrvOp:
			// variable result in an atomic operation sequence
			CPPAD_ASSERT_UNKNOWN( user_state == user_ret );
			CPPAD_ASSERT_UNKNOWN( 0 < user_i && user_i <= user_m );
			--user_i;
			var_sparsity.begin(i_var);
			i = var_sparsity.next_element();
			while( i < user_q )
			{	if( user_pack )
					pack_r[ user_i * user_q + i ] = true;
				if( user_bool )
					bool_r[ user_i * user_q + i ] = true;
				if( user_set )
					set_r[user_i].insert(i);
				i = var_sparsity.next_element();
			}
			if( user_i == 0 )
			{	// call users function for this operation
				user_atom->set_id(user_id);
				if( user_pack )
					CPPAD_ATOMIC_CALL( user_q, pack_r, pack_s);
				if( user_bool )
					CPPAD_ATOMIC_CALL( user_q, bool_r, bool_s);
				if( user_set )
					CPPAD_ATOMIC_CALL( user_q, set_r, set_s);
				user_state = user_arg;
			}
			break;
			// -------------------------------------------------

			case ZmulpvOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[1], var_sparsity
			);
			break;
			// -------------------------------------------------

			case ZmulvpOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			reverse_sparse_jacobian_unary_op(
				i_var, arg[0], var_sparsity
			);
			break;
			// -------------------------------------------------

			case ZmulvvOp:
			CPPAD_ASSERT_NARG_NRES(op, 2, 1);
			reverse_sparse_jacobian_binary_op(
				i_var, arg, var_sparsity
			);
			break;
			// -------------------------------------------------

			default:
			CPPAD_ASSERT_UNKNOWN(0);
		}
# if CPPAD_REV_JAC_SWEEP_TRACE
		for(j = 0; j < limit; j++)
			z_value[j] = false;
		var_sparsity.begin(i_var);
		j = var_sparsity.next_element();
		while( j < limit )
		{	z_value[j] = true;
			j          = var_sparsity.next_element();
		}
		printOp(
			std::cout,
			play,
			i_op,
			i_var,
			op,
			arg
		);
		if( NumRes(op) > 0 && op != BeginOp ) printOpResult(
			std::cout,
			0,
			(CppAD::vectorBool *) CPPAD_NULL,
			1,
			&z_value
		);
		std::cout << std::endl;
	}
	std::cout << std::endl;
# else
	}
# endif
	// values corresponding to BeginOp
	CPPAD_ASSERT_UNKNOWN( i_op == 0 );
	CPPAD_ASSERT_UNKNOWN( i_var == 0 );

	return;
}
} // END_CPPAD_NAMESPACE

// preprocessor symbols that are local to this file
# undef CPPAD_REV_JAC_SWEEP_TRACE
# undef CPPAD_ATOMIC_CALL

# endif
