
#pragma once

#include <cstddef>
#include <git2/types.h>
#include <git2/buffer.h>
#include <git2/status.h>
#include <git2/errors.h>

#include <filesystem>
#include <exception>
#include <boost/noncopyable.hpp>
#include <string_view>

namespace gitpp {

	namespace exception {

		struct git2_exception : public std::exception
		{
			virtual const char* what() const noexcept { return "libgit2 exception";}
		};

		struct not_repo : public git2_exception
		{
			virtual const char* what() const noexcept { return "not a git repo"; }
		};

		struct resolve_failed : public git2_exception
		{
			virtual const char* what() const noexcept { return "reference invalid"; }
		};

		struct error : public std::exception
		{
			error();
			virtual const char* what() const noexcept { return _e->message; }
			const git_error * _e;
		};
	}

	class oid
	{
		git_oid oid_;
	public:
		const git_oid* operator & () const
		{
			return &oid_;
		}

		git_oid* operator & ()
		{
			return &oid_;
		}

		oid ()
		{
			memset(&oid_, 0, sizeof (oid_));
		}

		explicit oid(const git_oid* git_oid_)
		{
			memcpy(&oid_, git_oid_, sizeof (oid_));
		}

		bool operator == (const oid& other) const;

		oid& operator = (const oid& other);

		std::string as_sha1_string() const;

		static oid from_sha1_string(std::string_view);
	};

	class reference
	{
		git_reference* ref = nullptr;
		bool owned = false;
	public:
		explicit reference(git_reference* ref);
		explicit reference(const git_reference* ref);
		~reference();

		reference(reference&&);
		reference(const reference&);

	public:
		git_reference_t type() const;
		oid target() const;
		reference resolve() const;
	};

	struct buf : boost::noncopyable
	{
		git_buf buf_;

		git_buf * operator & () noexcept
		{
			return &buf_;
		}

		operator std::string_view () noexcept;

		explicit buf(git_buf*) noexcept;
		buf() noexcept;
		~buf() noexcept;
	};

	class object
	{
	protected:
		git_object* obj_;
	public:
		virtual ~object();

		explicit object(git_object*);
		object(const object&);
		object(object&&);
	};

	class blob : public object
	{
	public:
		explicit blob(git_blob*);

		std::string_view get_content() const;
		std::size_t size() const;
	};

	class tree_entry
	{
		git_tree_entry * entry = nullptr;
		bool owned = false;
	public:
		explicit tree_entry(const git_tree_entry*);
		explicit tree_entry(git_tree_entry*);
		~tree_entry();

		tree_entry(const tree_entry& other);
		tree_entry(tree_entry&& other);

		bool empty() const;

	public:
		oid get_oid() const;

		git_object_t type() const;

		std::string name() const;
	};

	class tree : public object
	{
	public:
		explicit tree(git_tree*);

	public:
		class tree_iterator
		{
			friend class tree;
			const tree* parent;
			std::size_t index;

			tree_iterator(const tree* parent, std::size_t index);

		public:
			bool operator ==  (const tree_iterator & other) const = default;

			tree_iterator& operator++();

			tree_entry operator*();
		};

		tree_iterator begin() const;
		tree_iterator end() const;

		const git_tree* native_handle() const;
		git_tree* native_handle();

		tree_entry by_path(std::string path) const;

	};

	class status_list : boost::noncopyable
	{
	public:
		struct git_status_entry_iterator
		{
			git_status_entry_iterator(status_list& parent, std::size_t idx);

			const git_status_entry* operator*();

			git_status_entry_iterator& operator ++();

			bool operator == (const git_status_entry_iterator&) const;

			status_list & parent;
			int idx;
		};

	public:
		explicit status_list(git_status_list*);
		~status_list();

		git_status_list* native_handle();
		const git_status_list* native_handle() const;

		git_status_entry_iterator begin();
		git_status_entry_iterator end();

	public:
		std::size_t size() const;

	private:
		git_status_list* _status_list;
	};

	class index : boost::noncopyable
	{
	public:
		explicit index(git_index*);
		~index();
		git_index* native_handle();

		oid write_tree();

	private:
		git_index* _index;
	};

	class repo : boost::noncopyable
	{
		git_repository* repo_;
	public:
		explicit repo(git_repository*) noexcept;
		repo(std::filesystem::path repo_dir);                   // throws not_repo
		~repo() noexcept;

	public:
		git_repository* native_handle() { return repo_;}
		const git_repository* native_handle() const { return repo_;}

	public:
		index get_index();
		status_list new_status_list();
		reference head() const;
		tree get_tree_by_commit(oid);
		tree get_tree_by_treeid(oid);
		blob get_blob(oid) const;

		bool is_bare() const noexcept;

	};

	bool is_git_repo(std::filesystem::path);
	bool init_bare_repo(std::filesystem::path repo_path);

}
