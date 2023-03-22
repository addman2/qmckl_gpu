#include "include/qmckl_ao.h"
#include "include/qmckl_mo.h"
#ifdef HAVE_CUBLAS 
#include <cublas_v2.h>
#endif

//**********
// COMPUTE
//**********
//
#ifdef HAVE_CUBLAS
qmckl_exit_code
qmckl_compute_mo_basis_mo_vgl_cublas_device (const qmckl_context          context,
                                             const int64_t                ao_num,
                                             const int64_t                mo_num,
                                             const int64_t                point_num,
                                             const double* restrict       coefficient_t,
                                             const double* restrict       ao_vgl,
                                                   double* restrict const mo_vgl )
{
  assert (context != QMCKL_NULL_CONTEXT);

  cublasHandle_t handle;

  double const alpha =  1.;
  double const beta  =  0.;

  cublasOperation_t transa = CUBLAS_OP_N ;
  cublasOperation_t transb = CUBLAS_OP_N ;

  // NB: cublas views arrays as column-major (ie FORTRAN-like) ordered
  int const m = mo_num ; 
  int const k = ao_num ;
  int const n = point_num * 5 ;

  int const lda = m ;
  int const ldb = k ;
  int const ldc = m ;

 cublasCreate(&handle);
//    printf("co %p\n",coefficient_t);
//    printf("ao %p\n",ao_vgl);
//    printf("mo %p\n",mo_vgl);
// #pragma omp target data use_device_ptr(coefficient_t, ao_vgl, mo_vgl)
// {
//    double my_alpha = alpha;
//    double my_beta  = beta;
    cublasDgemm_v2( handle, transa, transb, m, n, k, &alpha, coefficient_t, lda, ao_vgl, ldb, &beta, mo_vgl, ldc );
// }
 cublasDestroy(handle);

  return QMCKL_SUCCESS;
}
#endif

/* mo_select */

// Forward declare this, as its needed by select_mo
qmckl_exit_code qmckl_finalize_mo_basis_device(qmckl_context_device context);

bool qmckl_mo_basis_select_mo_device(qmckl_context_device context,
									 int32_t *keep, int64_t size_max) {
	if (qmckl_context_check((qmckl_context)context) == QMCKL_NULL_CONTEXT) {
		return qmckl_failwith(context, QMCKL_INVALID_CONTEXT,
							  "qmckl_get_mo_basis_select_mo_device", NULL);
	}

	// WARNING Here, we are expecting a CPU array (instead of a GPU array
	// usually), because it will not be used as a data to be stored in the
	// context. Thus, it makes more sense (and is actually more efficient) to
	// use a CPU array.

	qmckl_context_struct *const ctx = (qmckl_context_struct *)context;
	assert(ctx != NULL);

	if (!(qmckl_mo_basis_provided(context))) {
		return qmckl_failwith(context, QMCKL_NOT_PROVIDED,
							  "qmckl_get_mo_basis_select_mo_device", NULL);
	}

	if (keep == NULL) {
		return qmckl_failwith(context, QMCKL_INVALID_ARG_2,
							  "qmckl_get_mo_basis_select_mo_device",
							  "NULL pointer");
	}

	const int64_t mo_num = ctx->mo_basis.mo_num;
	const int64_t ao_num = ctx->ao_basis.ao_num;

	if (size_max < mo_num) {
		return qmckl_failwith(context, QMCKL_INVALID_ARG_3,
							  "qmckl_get_mo_basis_select_mo",
							  "Array too small: expected mo_num.");
	}

	int64_t mo_num_new = 0;
	for (int64_t i = 0; i < mo_num; ++i) {
		if (keep[i] != 0)
			++mo_num_new;
	}

	double *restrict coefficient = (double *)qmckl_malloc_device(
		context, ao_num * mo_num_new * sizeof(double));

	int64_t k = 0;
	for (int64_t i = 0; i < mo_num; ++i) {
		if (keep[i] != 0) {
			qmckl_memcpy_D2D(context, &(coefficient[k * ao_num]),
							 &(ctx->mo_basis.coefficient[i * ao_num]),
							 ao_num * sizeof(double));
			++k;
		}
	}

	qmckl_exit_code rc = qmckl_free_device(context, ctx->mo_basis.coefficient);
	if (rc != QMCKL_SUCCESS)
		return rc;

	ctx->mo_basis.coefficient = coefficient;
	ctx->mo_basis.mo_num = mo_num_new;

	rc = qmckl_finalize_mo_basis_device(context);
	return rc;
}

//**********
// PROVIDE
//**********

/* mo_vgl */

qmckl_exit_code qmckl_provide_mo_basis_mo_vgl_device(qmckl_context context) {

	qmckl_exit_code rc = QMCKL_SUCCESS;

	if (qmckl_context_check((qmckl_context)context) == QMCKL_NULL_CONTEXT) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_CONTEXT,
							  "qmckl_provide_mo_basis_mo_vgl_device", NULL);
	}

	qmckl_context_struct *const ctx = (qmckl_context_struct *)context;
	assert(ctx != NULL);

	if (!ctx->mo_basis.provided) {
		return qmckl_failwith(context, QMCKL_NOT_PROVIDED,
							  "qmckl_provide_mo_basis_mo_vgl_device", NULL);
	}

	/* Compute if necessary */
	if (ctx->point.date > ctx->mo_basis.mo_vgl_date) {

		/* Allocate array */
		if (ctx->mo_basis.mo_vgl == NULL) {

			double *mo_vgl = (double *)qmckl_malloc_device(
				context,
				5 * ctx->mo_basis.mo_num * ctx->point.num * sizeof(double));

			if (mo_vgl == NULL) {
				return qmckl_failwith(context, QMCKL_ALLOCATION_FAILED,
									  "qmckl_mo_basis_mo_vgl", NULL);
			}
			ctx->mo_basis.mo_vgl = mo_vgl;
		}

		rc = qmckl_provide_ao_basis_ao_vgl_device(context);
		if (rc != QMCKL_SUCCESS) {
			return qmckl_failwith(context, QMCKL_NOT_PROVIDED, "qmckl_ao_basis",
								  NULL);
		}

#ifdef HAVE_CUBLAS
		rc = qmckl_compute_mo_basis_mo_vgl_cublas_device(
			context, ctx->ao_basis.ao_num, ctx->mo_basis.mo_num, ctx->point.num,
			ctx->mo_basis.coefficient_t, ctx->ao_basis.ao_vgl,
			ctx->mo_basis.mo_vgl);
#else
		rc = qmckl_compute_mo_basis_mo_vgl_device(
			context, ctx->ao_basis.ao_num, ctx->mo_basis.mo_num, ctx->point.num,
			ctx->mo_basis.coefficient_t, ctx->ao_basis.ao_vgl,
			ctx->mo_basis.mo_vgl);
#endif

		if (rc != QMCKL_SUCCESS) {
			return rc;
		}

		ctx->mo_basis.mo_vgl_date = ctx->date;
	}

	return QMCKL_SUCCESS;
}

/* mo_value */

qmckl_exit_code qmckl_provide_mo_basis_mo_value_device(qmckl_context context) {

	qmckl_exit_code rc = QMCKL_SUCCESS;

	if (qmckl_context_check((qmckl_context)context) == QMCKL_NULL_CONTEXT) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_CONTEXT,
							  "qmckl_provide_mo_basis_mo_value_device", NULL);
	}

	qmckl_context_struct *const ctx = (qmckl_context_struct *)context;
	assert(ctx != NULL);

	if (!ctx->mo_basis.provided) {
		return qmckl_failwith(context, QMCKL_NOT_PROVIDED,
							  "qmckl_provide_mo_basis_mo_value_device", NULL);
	}

	/* Compute if necessary */
	if (ctx->point.date > ctx->mo_basis.mo_value_date) {

		qmckl_exit_code rc;

		/* Allocate array */
		if (ctx->mo_basis.mo_value == NULL) {

			double *mo_value = (double *)qmckl_malloc_device(
				context,
				ctx->mo_basis.mo_num * ctx->point.num * sizeof(double));

			if (mo_value == NULL) {
				return qmckl_failwith(context, QMCKL_ALLOCATION_FAILED,
									  "qmckl_mo_basis_mo_value", NULL);
			}
			ctx->mo_basis.mo_value = mo_value;
		}

		if (ctx->mo_basis.mo_vgl_date == ctx->point.date) {

			// mo_vgl has been computed at this step: Just copy the data.

			double *v = &(ctx->mo_basis.mo_value[0]);
			double *vgl = &(ctx->mo_basis.mo_vgl[0]);
#pragma omp target is_device_ptr(v, vgl)
			{
				for (int i = 0; i < ctx->point.num; ++i) {
					for (int k = 0; k < ctx->mo_basis.mo_num; ++k) {
						v[k] = vgl[k];
					}
					v += ctx->mo_basis.mo_num;
					vgl += ctx->mo_basis.mo_num * 5;
				}
			}

		} else {

			rc = qmckl_provide_ao_basis_ao_vgl_device(context);
			if (rc != QMCKL_SUCCESS) {
				return qmckl_failwith(context, QMCKL_NOT_PROVIDED,
									  "qmckl_ao_basis_ao_vgl_device", NULL);
			}

			rc = qmckl_compute_mo_basis_mo_value_device(
				context, ctx->ao_basis.ao_num, ctx->mo_basis.mo_num,
				ctx->point.num, ctx->mo_basis.coefficient_t,
				ctx->ao_basis.ao_value, ctx->mo_basis.mo_value);
		}

		if (rc != QMCKL_SUCCESS) {
			return rc;
		}

		ctx->mo_basis.mo_value_date = ctx->date;
	}

	return QMCKL_SUCCESS;
}

//**********
// GET
//**********

/* mo_vgl */

qmckl_exit_code qmckl_get_mo_basis_mo_vgl_device(qmckl_context context,
												 double *const mo_vgl,
												 const int64_t size_max) {

	if (qmckl_context_check((qmckl_context)context) == QMCKL_NULL_CONTEXT) {
		return QMCKL_NULL_CONTEXT;
	}

	qmckl_exit_code rc;

	rc = qmckl_provide_mo_basis_mo_vgl_device(context);
	if (rc != QMCKL_SUCCESS)
		return rc;

	qmckl_context_struct *const ctx = (qmckl_context_struct *)context;
	assert(ctx != NULL);

	int64_t sze = 5 * ctx->point.num * ctx->mo_basis.mo_num;
	if (size_max < sze) {
		return qmckl_failwith(context, QMCKL_INVALID_ARG_3,
							  "qmckl_get_mo_basis_mo_vgl",
							  "input array too small");
	}
	qmckl_memcpy_D2D(context, mo_vgl, ctx->mo_basis.mo_vgl,
					 sze * sizeof(double));

	return QMCKL_SUCCESS;
}

qmckl_exit_code qmckl_get_mo_basis_mo_vgl_inplace_device(
	qmckl_context context, double *const mo_vgl, const int64_t size_max) {

	if (qmckl_context_check((qmckl_context)context) == QMCKL_NULL_CONTEXT) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_CONTEXT,
							  "qmckl_get_mo_basis_mo_vgl_device", NULL);
	}

	qmckl_exit_code rc;

	qmckl_context_struct *const ctx = (qmckl_context_struct *)context;
	assert(ctx != NULL);

	const int64_t sze = 5 * ctx->mo_basis.mo_num * ctx->point.num;
	if (size_max < sze) {
		return qmckl_failwith(context, QMCKL_INVALID_ARG_3,
							  "qmckl_get_mo_basis_mo_vgl_device",
							  "input array too small");
	}

	rc = qmckl_context_touch_device(context);
	if (rc != QMCKL_SUCCESS)
		return rc;

	double *old_array = ctx->mo_basis.mo_vgl;

	ctx->mo_basis.mo_vgl = mo_vgl;

	rc = qmckl_provide_mo_basis_mo_vgl_device(context);
	if (rc != QMCKL_SUCCESS)
		return rc;

	ctx->mo_basis.mo_vgl = old_array;

	return QMCKL_SUCCESS;
}

/* mo_value */

qmckl_exit_code qmckl_get_mo_basis_mo_value_device(qmckl_context context,
												   double *const mo_value,
												   const int64_t size_max) {

	if (qmckl_context_check((qmckl_context)context) == QMCKL_NULL_CONTEXT) {
		return QMCKL_NULL_CONTEXT;
	}

	qmckl_exit_code rc;

	rc = qmckl_provide_mo_basis_mo_value_device(context);
	if (rc != QMCKL_SUCCESS)
		return rc;

	qmckl_context_struct *const ctx = (qmckl_context_struct *)context;
	assert(ctx != NULL);

	const int64_t sze = ctx->point.num * ctx->mo_basis.mo_num;
	if (size_max < sze) {
		return qmckl_failwith(context, QMCKL_INVALID_ARG_3,
							  "qmckl_get_mo_basis_mo_value",
							  "input array too small");
	}
	qmckl_memcpy_D2D(context, mo_value, ctx->mo_basis.mo_value,
					 sze * sizeof(double));

	return QMCKL_SUCCESS;
}

qmckl_exit_code qmckl_get_mo_basis_mo_value_inplace_device(
	qmckl_context context, double *const mo_value, const int64_t size_max) {

	if (qmckl_context_check((qmckl_context)context) == QMCKL_NULL_CONTEXT) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_CONTEXT,
							  "qmckl_get_mo_basis_mo_value_device", NULL);
	}

	qmckl_exit_code rc;

	qmckl_context_struct *const ctx = (qmckl_context_struct *)context;
	assert(ctx != NULL);

	const int64_t sze = ctx->mo_basis.mo_num * ctx->point.num;
	if (size_max < sze) {
		return qmckl_failwith(context, QMCKL_INVALID_ARG_3,
							  "qmckl_get_mo_basis_mo_value_device",
							  "input array too small");
	}

	rc = qmckl_context_touch_device(context);
	if (rc != QMCKL_SUCCESS)
		return rc;

	double *old_array = ctx->mo_basis.mo_value;

	ctx->mo_basis.mo_value = mo_value;

	rc = qmckl_provide_mo_basis_mo_value_device(context);
	if (rc != QMCKL_SUCCESS)
		return rc;

	ctx->mo_basis.mo_value = old_array;

	return QMCKL_SUCCESS;
}

/* Provided check  */

bool qmckl_mo_basis_provided(qmckl_context_device context) {

	if (qmckl_context_check(context) == QMCKL_NULL_CONTEXT) {
		return false;
	}

	qmckl_context_struct *const ctx = (qmckl_context_struct *)context;
	assert(ctx != NULL);

	return ctx->mo_basis.provided;
}
