#include "../include/qmckl_memory.h"

// This file contains functions prototypes for context memory management
// functions (on device only, we expect most if not all of the context
// memory to be allocated on device in most cases)
// (OpenACC implementations)

//**********
// ALLOCS / FREES
//**********

void *qmckl_malloc_device(qmckl_context_device context,
						  const qmckl_memory_info_struct info) {

	assert(qmckl_context_check((qmckl_context)context) != QMCKL_NULL_CONTEXT);

	qmckl_context_struct *const ctx = (qmckl_context_struct *)context;
	qmckl_context_device_struct *const ds =
		(qmckl_context_device_struct *)ctx->qmckl_extra;
	// int device_id = qmckl_get_device_id(context);

	/* Allocate memory and zero it */
	void *pointer = acc_malloc(info.size);
	if (pointer == NULL) {
		return NULL;
	}

	// TODO
	// Memset to 0 of size info.size
	// memset(pointer, 0, info.size);

	qmckl_lock((qmckl_context)context);
	{

		/* If qmckl_memory_struct is full, reallocate a larger one */
		if (ds->memory.n_allocated == ds->memory.array_size) {
			const size_t old_size = ds->memory.array_size;
			qmckl_memory_info_struct *new_array =
				realloc(ds->memory.element,
						2L * old_size * sizeof(qmckl_memory_info_struct));
			if (new_array == NULL) {
				qmckl_unlock(context);
				free(pointer);
				return NULL;
			}

			memset(&(new_array[old_size]), 0,
				   old_size * sizeof(qmckl_memory_info_struct));
			ds->memory.element = new_array;
			ds->memory.array_size = 2L * old_size;
		}

		/* Find first NULL entry */
		size_t pos = (size_t)0;
		while (pos < ds->memory.array_size &&
			   ds->memory.element[pos].size > (size_t)0) {
			pos += (size_t)1;
		}
		assert(ds->memory.element[pos].size == (size_t)0);

		/* Copy info at the new location */
		ds->memory.element[pos].size = info.size;
		ds->memory.element[pos].pointer = pointer;
		ds->memory.n_allocated += (size_t)1;
	}
	qmckl_unlock((qmckl_context)context);

	return pointer;
}

qmckl_exit_code qmckl_free_device(qmckl_context_device context,
								  void *const ptr) {

	if (qmckl_context_check((qmckl_context)context) == QMCKL_NULL_CONTEXT) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_CONTEXT,
							  "qmckl_free_device", NULL);
	}

	if (ptr == NULL) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_ARG_2,
							  "qmckl_free_device", "NULL pointer");
	}

	qmckl_context_struct *const ctx = (qmckl_context_struct *)context;
	qmckl_context_device_struct *const ds =
		(qmckl_context_device_struct *)ctx->qmckl_extra;
	// int device_id = qmckl_get_device_id(context);

	qmckl_lock((qmckl_context)context);
	{
		/* Find pointer in array of saved pointers */
		size_t pos = (size_t)0;
		while (pos < ds->memory.array_size &&
			   ds->memory.element[pos].pointer != ptr) {
			pos += (size_t)1;
		}

		if (pos >= ds->memory.array_size) {
			/* Not found */
			qmckl_unlock(context);
			return qmckl_failwith((qmckl_context)context, QMCKL_FAILURE,
								  "qmckl_free_device",
								  "Pointer not found in context");
		}

		acc_free(ptr);

		memset(&(ds->memory.element[pos]), 0, sizeof(qmckl_memory_info_struct));
		ds->memory.n_allocated -= (size_t)1;
	}
	qmckl_unlock((qmckl_context)context);

	return QMCKL_SUCCESS;
}

//**********
// MEMCPYS
//**********

qmckl_exit_code qmckl_memcpy_H2D(qmckl_context_device context, void *const dest,
								 void *const src, size_t size) {

	if (qmckl_context_check((qmckl_context)context) == QMCKL_NULL_CONTEXT) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_CONTEXT,
							  "qmckl_memcpy_H2D", NULL);
	}

	if (dest == NULL) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_ARG_2,
							  "qmckl_memcpu_H2D", "NULL dest pointer");
	}

	if (src == NULL) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_ARG_3,
							  "qmckl_memcpu_H2D", "NULL src pointer");
	}

	// int device_id = qmckl_get_device_id(context);

	qmckl_lock((qmckl_context)context);
	{
		int ret = acc_memcpy_to_device(dest, src, size);
		if (ret) {
			return qmckl_failwith((qmckl_context)context, QMCKL_FAILURE,
								  "qmckl_memcpy_H2D",
								  "Call to acc_memcpy_to_device failed");
		}
	}
	qmckl_unlock((qmckl_context)context);

	return QMCKL_SUCCESS;
}

qmckl_exit_code qmckl_memcpy_D2H(qmckl_context_device context, void *const dest,
								 void *const src, size_t size) {

	if (qmckl_context_check((qmckl_context)context) == QMCKL_NULL_CONTEXT) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_CONTEXT,
							  "qmckl_memcpy_D2H", NULL);
	}

	if (dest == NULL) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_ARG_2,
							  "qmckl_memcpy_D2H", "NULL dest pointer");
	}

	if (src == NULL) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_ARG_3,
							  "qmckl_memcpy_D2H", "NULL src pointer");
	}

	// int device_id = qmckl_get_device_id(context);

	qmckl_lock((qmckl_context)context);
	{
		int ret = acc_memcpy_from_device(dest, src, size);
		if (ret) {
			return qmckl_failwith((qmckl_context)context, QMCKL_FAILURE,
								  "qmckl_memcpy_D2H",
								  "Call to acc_memcpy_to_host failed");
		}
	}
	qmckl_unlock((qmckl_context)context);

	return QMCKL_SUCCESS;
}

qmckl_exit_code qmckl_memcpy_D2D(qmckl_context_device context, void *const dest,
								 void *const src, size_t size) {

	if (qmckl_context_check((qmckl_context)context) == QMCKL_NULL_CONTEXT) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_CONTEXT,
							  "qmckl_memcpy_D2D", NULL);
	}

	if (dest == NULL) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_ARG_2,
							  "qmckl_memcpy_D2D", "NULL dest pointer");
	}

	if (src == NULL) {
		return qmckl_failwith((qmckl_context)context, QMCKL_INVALID_ARG_3,
							  "qmckl_memcpy_D2D", "NULL src pointer");
	}

	// int device_id = qmckl_get_device_id(context);

	qmckl_lock((qmckl_context)context);
	{
		int ret = acc_memcpy_to_device(dest, src, size);
		if (ret) {
			return qmckl_failwith((qmckl_context)context, QMCKL_FAILURE,
								  "qmckl_memcpy_D2D",
								  "Call to acc_memcpy_to_device failed");
		}
	}
	qmckl_unlock((qmckl_context)context);

	return QMCKL_SUCCESS;
}
