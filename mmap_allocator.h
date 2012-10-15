#ifndef _MMAP_ALLOCATOR_H
#define _MMAP_ALLOCATOR_H

#include <memory>
#include <string>
#include <stdio.h>
#include <vector>
#include "mmap_access_mode.h"
#include "mmap_file_pool.h"

namespace mmap_allocator_namespace
{
	template <typename T, typename A>
        class mmappable_vector;

	template <typename T> 
	class mmap_allocator: public std::allocator<T>
	{
public:
		typedef size_t size_type;
		typedef off_t offset_type;
		typedef T* pointer;
		typedef const T* const_pointer;

		template<typename _Tp1>
	        struct rebind
        	{ 
			typedef mmap_allocator<_Tp1> other; 
		};

		pointer allocate(size_type n, const void *hint=0)
		{
			void *the_pointer;
			if (get_verbosity() > 0) {
				fprintf(stderr, "Alloc %d bytes.\n", n*sizeof(T));
			}
			if (access_mode == DEFAULT_STL_ALLOCATOR) {
				return std::allocator<T>::allocate(n, hint);
			} else {
				if (bypass_file_pool) {
					the_pointer = private_file.open_and_mmap_file(filename, access_mode, offset, n*sizeof(T), map_whole_file, allow_remap);
				} else {
					the_pointer = the_pool.mmap_file(filename, access_mode, offset, n*sizeof(T), map_whole_file, allow_remap);
				}
				if (the_pointer == NULL) {
					throw(mmap_allocator_exception("Couldn't mmap file, mmap_file returned NULL"));
				}
				if (get_verbosity() > 0) {
					fprintf(stderr, "pointer = %p\n", the_pointer);
				}
				
				return (pointer)the_pointer;
			}
		}

		void deallocate(pointer p, size_type n)
		{
			if (get_verbosity() > 0) {
				fprintf(stderr, "Dealloc %d bytes (%p).\n", n*sizeof(T), p);
			}
			if (access_mode == DEFAULT_STL_ALLOCATOR) {
				std::allocator<T>::deallocate(p, n);
			} else {
				if (bypass_file_pool) {
					private_file.munmap_and_close_file();
				} else {
					the_pool.munmap_file(filename, access_mode, offset, n*sizeof(T));
				}
			}
		}

		mmap_allocator() throw(): 
			std::allocator<T>(),
			filename(""),
			offset(0),
			access_mode(DEFAULT_STL_ALLOCATOR),
			map_whole_file(false),
			allow_remap(false),
			bypass_file_pool(false),
			private_file()
		{ }

		mmap_allocator(const std::allocator<T> &a) throw():
			std::allocator<T>(a),
			filename(""),
			offset(0),
			access_mode(DEFAULT_STL_ALLOCATOR),
			map_whole_file(false),
			allow_remap(false),
			bypass_file_pool(false),
			private_file()
		{ }

		mmap_allocator(const mmap_allocator &a) throw():
			std::allocator<T>(a),
			filename(a.filename),
			offset(a.offset),
			access_mode(a.access_mode),
			map_whole_file(a.map_whole_file),
			allow_remap(a.allow_remap),
			bypass_file_pool(a.bypass_file_pool),
			private_file(a.private_file)
		{ }
		mmap_allocator(const std::string filename_param, enum access_mode access_mode_param = READ_ONLY, offset_type offset_param = 0, int flags = 0) throw():
			std::allocator<T>(),
			filename(filename_param),
			offset(offset_param),
			access_mode(access_mode_param),
			map_whole_file(flags | MAP_WHOLE_FILE != 0),
			allow_remap(flags | ALLOW_REMAP != 0),
			bypass_file_pool(flags | BYPASS_FILE_POOL != 0),
			private_file()
		{
		}
			
		~mmap_allocator() throw() { }

private:
		friend class mmappable_vector<T, mmap_allocator<T> >;

		std::string filename;
		offset_type offset;
		enum access_mode access_mode;
		bool map_whole_file;
		bool allow_remap;
		bool bypass_file_pool;
		mmapped_file private_file;  /* used if bypass is set */
	};

	template <typename T, typename A = mmap_allocator<T> > 
	class mmappable_vector: public std::vector<T, A> {
public:
/* TODO: are these necessary here? */
		typedef typename std::vector<T, A>::const_iterator const_iterator;
		typedef typename std::vector<T, A>::iterator iterator;
		typedef T value_type;
		typedef A allocator_type;

		mmappable_vector():
			std::vector<T,A>()
		{
		}

		mmappable_vector(const mmappable_vector<T, A> &other):
			std::vector<T,A>(other)
		{
		}

		explicit mmappable_vector(size_t n):
			std::vector<T,A>()
		{
			mmap_file(n);
		}

		explicit mmappable_vector(A alloc):
			std::vector<T,A>(alloc)
		{
		}

		mmappable_vector(iterator from, iterator to):
			std::vector<T,A>(from, to)
		{
		}
		
		mmappable_vector(typename std::vector<T,A>::iterator from, typename std::vector<T,A>::iterator to):
			std::vector<T,A>(from, to)
		{
		}
		
		mmappable_vector(int n, T val, A alloc):
			std::vector<T,A>(n, val, alloc)
		{
		}

		mmappable_vector(int n, T val):
			std::vector<T,A>(n, val)
		{
		}

/* Use this only when the allocator is already initialized. */
		void mmap_file(size_t n)
		{
			std::vector<T,A>::reserve(n);
			_M_set_finish(n);
		}

		void mmap_file(std::string filename, enum access_mode access_mode, const off_t offset, const size_t n, int flags = 0)
		{
			if (std::vector<T,A>::size() > 0) {
				throw mmap_allocator_exception("Remapping currently not implemented.");
			}
#ifdef __GNUC__
			A &the_allocator = std::vector<T,A>::_M_get_Tp_allocator();
#else
#error "Not GNU C++, please either implement me or use GCC"
#endif		
			the_allocator.filename = filename;
			the_allocator.offset = offset;
			the_allocator.access_mode = access_mode;
			the_allocator.map_whole_file = (flags | MAP_WHOLE_FILE) != 0;
			the_allocator.allow_remap = (flags | ALLOW_REMAP) != 0;
			the_allocator.bypass_file_pool = (flags | BYPASS_FILE_POOL) != 0;

			mmap_file(n);
		}

		void munmap_file(void)
		{
			size_t n = std::vector<T,A>::size();
#ifdef __GNUC__
			std::vector<T,A>::_M_deallocate(std::vector<T,A>::_M_impl._M_start, n);
			std::vector<T,A>::_M_impl._M_start = 0;
			std::vector<T,A>::_M_impl._M_finish = 0;
			std::vector<T,A>::_M_impl._M_end_of_storage = 0;
#else
#error "Not GNU C++, please either implement me or use GCC"
#endif
		}

private:
		void _M_set_finish(size_t n)
		{
#ifdef __GNUC__
			std::vector<T,A>::_M_impl._M_finish = std::vector<T,A>::_M_impl._M_start + n;
#else
#error "Not GNU C++, please either implement me or use GCC"
#endif		
		}
	};

	template <typename T> 
	std::vector<T> to_std_vector(const mmappable_vector<T> &v)
	{
		return std::vector<T>(v.begin(), v.end());
	}

	template <typename T> 
	mmappable_vector<T> to_mmappable_vector(std::vector<T> &v)
	{
		return mmappable_vector<T>(v.begin(), v.end());
	}
}

#endif /* _MMAP_ALLOCATOR_H */
