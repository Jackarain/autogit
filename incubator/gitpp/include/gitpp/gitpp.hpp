﻿#pragma once

#include <cstddef>
#include <git2/types.h>
#include <git2/buffer.h>
#include <git2/status.h>
#include <git2/errors.h>

#include <filesystem>
#include <exception>
#include <boost/noncopyable.hpp>
#include <vector>
#include <string>
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
			virtual const char* what() const noexcept { return e_->message; }
			const git_error * e_;
		};
	}

	class repo;
	class oid;
	class reference;
	class object;
	class blob;
	class commit;
	class tree;
	class status_list;

	class oid
	{
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

	private:
		git_oid oid_;
	};

	class reference
	{
		git_reference* ref_ = nullptr;
		bool owned_ = false;

	public:
		explicit reference(git_reference* ref);
		explicit reference(const git_reference* ref);
		~reference();

		reference(reference&&);
		reference(const reference&);

		bool operator==(const reference& other) const;

	public:
		git_reference_t type() const;
		oid target() const;
		reference resolve() const;
	};

#if 0
	struct buf : boost::noncopyable
	{
		git_buf buf_;

		git_buf* operator&() noexcept
		{
			return &buf_;
		}

		operator std::string_view() noexcept;
		explicit buf(git_buf*) noexcept;

		buf() noexcept;
		~buf() noexcept;
	};
#endif

	class object
	{
	public:
		object() = default;
		virtual ~object();

		explicit object(git_object*);
		object(const object&);
		object(object&&);

		oid get_oid() const;

	protected:
		git_object* obj_;
	};

	class blob : public object
	{
	public:
		explicit blob(git_blob*);
		std::string_view get_content() const;
		std::size_t size() const;
	};

	class commit : public object
	{
	public:
		explicit commit(git_commit*);
		git_commit* native_handle();
		const git_commit* native_handle() const;

		std::string message() const;
	};

	class tree_entry
	{
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

	private:
		git_tree_entry* entry_ = nullptr;
		bool owned_ = false;
	};

	class tree : public object
	{
	public:
		explicit tree(git_tree*);

	public:
		class tree_iterator
		{
		public:
			bool operator ==  (const tree_iterator & other) const = default;
			tree_iterator& operator++();
			tree_entry operator*();

		private:
			tree_iterator(const tree* parent, std::size_t index);

		private:
			friend class tree;
			const tree* parent_;
			std::size_t index_;
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

			status_list& parent_;
			std::size_t idx_;
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
		git_status_list* status_list_;
	};

	class index : boost::noncopyable
	{
	public:
		explicit index(repo* belong, git_index*);
		~index();
		git_index* native_handle();

		tree write_tree();

	private:
		repo* belong_;
		git_index* index_;
	};

	class signature
	{
	public:
		explicit signature(const signature&);
		signature(signature&&);
		signature(const std::string& name,  const std::string& email);
		~signature();
		git_signature* native_handle();
		const git_signature* native_handle() const;
		signature& operator = (const signature&);
		signature& operator = (signature&&);

	private:
		git_signature* git_sig_;
	};

	class remote
	{
	public:
		explicit remote(git_remote*);
		remote(const remote&);
		remote(remote&&);
		~remote();

		remote& operator = (const remote&);
		remote& operator = (remote&&);

		git_remote* native_handle();

	private:
		git_remote* git_remote_;
	};

	class repo : boost::noncopyable
	{
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
		remote get_remote(const std::string& remote_name);

		commit lookup_commit(oid);

		commit create_commit(const std::string& update_ref,
			const signature& author,
			const signature& committer,
			const std::string& message,
			const tree& tree,
			commit parent
		);
		commit create_commit(const std::string& update_ref,
			const signature& author,
			const signature& committer,
			const std::string& message,
			const tree& tree,
			std::vector<commit> parents
		);

		tree get_tree_by_commit(oid);
		tree get_tree_by_treeid(oid);
		blob get_blob(oid) const;

		bool is_bare() const noexcept;

	private:
		git_repository* repo_;
	};

	bool is_git_repo(std::filesystem::path);
	bool init_git_repo(std::filesystem::path repo_path,
		const std::string& url = "", bool bare = false);
}
