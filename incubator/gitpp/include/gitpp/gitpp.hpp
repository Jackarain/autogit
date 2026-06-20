#pragma once

#include <cstddef>
#include <git2/types.h>
#include <git2/oid.h>
#include <git2/buffer.h>
#include <git2/status.h>
#include <git2/errors.h>
#include <git2/remote.h>
#include <git2/branch.h>

#include <filesystem>
#include <exception>
#include <boost/noncopyable.hpp>
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <functional>

namespace gitpp {

	// -----------------------------------------------------------------------
	// Exception types
	// -----------------------------------------------------------------------

	namespace exception {

		struct git2_exception : public std::exception
		{
			const char* what() const noexcept override { return "libgit2 exception"; }
		};

		struct not_repo : public git2_exception
		{
			const char* what() const noexcept override { return "not a git repo"; }
		};

		struct resolve_failed : public git2_exception
		{
			const char* what() const noexcept override { return "reference invalid"; }
		};

		/// Stores a copy of the last libgit2 error message so it remains
		/// valid even after the thread-local error state is cleared.
		class error : public std::exception
		{
		public:
			error();
			const char* what() const noexcept override { return msg_.c_str(); }

		private:
			std::string msg_;
		};

		/// Thrown when a libgit2 call returns an unexpected error code.
		inline void throw_on_error(int code)
		{
			if (code < 0)
				throw error();
		}
	}

	// -----------------------------------------------------------------------
	// Forward declarations
	// -----------------------------------------------------------------------

	class repo;
	class oid;
	class reference;
	class object;
	class blob;
	class commit;
	class tree;
	class tree_entry;
	class status_list;
	class index;
	class signature;
	class remote;
	class revwalk;
	class branch_iterator;
	class tag;
	class diff;

	// -----------------------------------------------------------------------
	// oid  –  lightweight Git object ID wrapper
	// -----------------------------------------------------------------------

	class oid
	{
	public:
		const git_oid* operator&() const noexcept { return &oid_; }
		git_oid* operator&() noexcept { return &oid_; }

		oid() noexcept { memset(&oid_, 0, sizeof(oid_)); }

		explicit oid(const git_oid* src) noexcept
		{
			if (src) memcpy(&oid_, src, sizeof(oid_));
			else     memset(&oid_, 0, sizeof(oid_));
		}

		bool operator==(const oid& other) const noexcept;
		bool operator!=(const oid& other) const noexcept { return !(*this == other); }
		oid& operator=(const oid& other) noexcept;

		/// Returns true when the oid is all-zero (null oid).
		bool is_zero() const noexcept;

		std::string   as_sha1_string() const;
		static oid    from_sha1_string(std::string_view s);

	private:
		git_oid oid_;
	};

	// -----------------------------------------------------------------------
	// reference  –  Git reference (branch / HEAD / tag)
	// -----------------------------------------------------------------------

	class reference
	{
	public:
		explicit reference(git_reference* ref) noexcept;
		explicit reference(const git_reference* ref) noexcept;
		~reference() noexcept;

		reference(reference&& other) noexcept;
		reference(const reference& other);

		reference& operator=(reference&& other) noexcept;
		reference& operator=(const reference& other);

		bool operator==(const reference& other) const noexcept;
		bool operator!=(const reference& other) const noexcept { return !(*this == other); }

		git_reference_t type() const noexcept;
		oid            target() const;
		reference      resolve() const;

		std::string    name() const;
		std::string    shorthand() const;

		explicit operator bool() const noexcept { return ref_ != nullptr; }

	private:
		git_reference* ref_ = nullptr;
		bool owned_ = false;
	};

	// -----------------------------------------------------------------------
	// object  –  base for all git object types
	// -----------------------------------------------------------------------

	class object
	{
	public:
		object() noexcept : obj_(nullptr) {}
		virtual ~object();

		explicit object(git_object* o) noexcept;
		object(const object& o);
		object(object&& o) noexcept;

		object& operator=(object&& o) noexcept;
		object& operator=(const object& o);

		oid get_oid() const;

	protected:
		git_object* obj_ = nullptr;
	};

	// -----------------------------------------------------------------------
	// blob
	// -----------------------------------------------------------------------

	class blob : public object
	{
	public:
		explicit blob(git_blob* b) noexcept;
		std::string_view get_content() const noexcept;
		std::size_t size() const noexcept;
	};

	// -----------------------------------------------------------------------
	// commit
	// -----------------------------------------------------------------------

	class commit : public object
	{
	public:
		explicit commit(git_commit* c) noexcept;
		git_commit*       native_handle() noexcept;
		const git_commit* native_handle() const noexcept;

		std::string_view message() const noexcept;
		oid             tree_id() const;
		git_time_t      time() const;
	};

	// -----------------------------------------------------------------------
	// tree_entry
	// -----------------------------------------------------------------------

	class tree_entry
	{
	public:
		explicit tree_entry(const git_tree_entry* entry) noexcept;
		explicit tree_entry(git_tree_entry* entry) noexcept;
		~tree_entry() noexcept;

		tree_entry(const tree_entry& other) noexcept;
		tree_entry(tree_entry&& other) noexcept;

		tree_entry& operator=(const tree_entry& other) noexcept;
		tree_entry& operator=(tree_entry&& other) noexcept;

		bool empty() const noexcept { return entry_ == nullptr; }
		explicit operator bool() const noexcept { return entry_ != nullptr; }

		oid           get_oid() const;
		git_object_t  type() const noexcept;
		std::string_view name() const noexcept;

	private:
		git_tree_entry* entry_ = nullptr;
		bool owned_ = false;

		void release() noexcept;
	};

	// -----------------------------------------------------------------------
	// tree
	// -----------------------------------------------------------------------

	class tree : public object
	{
	public:
		explicit tree(git_tree* t) noexcept;

		class tree_iterator
		{
		public:
			bool operator==(const tree_iterator& other) const noexcept
			{
				return parent_ == other.parent_ && index_ == other.index_;
			}
			bool operator!=(const tree_iterator& other) const noexcept
			{
				return !(*this == other);
			}
			tree_iterator& operator++() noexcept;
			tree_entry operator*() const noexcept;

		private:
			friend class tree;
			tree_iterator(const tree* parent, std::size_t index) noexcept;

			const tree* parent_ = nullptr;
			std::size_t index_ = 0;
		};

		tree_iterator begin() const noexcept;
		tree_iterator end() const noexcept;

		const git_tree* native_handle() const noexcept;
		git_tree*       native_handle() noexcept;

		tree_entry by_path(std::string_view path) const;
	};

	// -----------------------------------------------------------------------
	// status_list
	// -----------------------------------------------------------------------

	class status_list : boost::noncopyable
	{
	public:
		struct git_status_entry_iterator
		{
			git_status_entry_iterator(status_list& parent, std::size_t idx) noexcept;

			const git_status_entry* operator*() const noexcept;

			git_status_entry_iterator& operator++() noexcept;

			bool operator==(const git_status_entry_iterator& other) const noexcept;
			bool operator!=(const git_status_entry_iterator& other) const noexcept
			{
				return !(*this == other);
			}

			status_list& parent_;
			std::size_t  idx_;
		};

	public:
		explicit status_list(git_status_list* sl) noexcept;
		~status_list() noexcept;

		git_status_list*       native_handle() noexcept;
		const git_status_list* native_handle() const noexcept;

		git_status_entry_iterator begin() noexcept;
		git_status_entry_iterator end() noexcept;

		std::size_t size() const noexcept;

	private:
		git_status_list* status_list_ = nullptr;
	};

	// -----------------------------------------------------------------------
	// index  (staging area)
	// -----------------------------------------------------------------------

	class index : boost::noncopyable
	{
	public:
		explicit index(repo* belong, git_index* gi) noexcept;
		~index() noexcept;

		git_index* native_handle() noexcept;

		tree   write_tree();
		void   add_by_path(const std::string& path);
		void   remove_by_path(const std::string& path);
		void   write() const;

	private:
		repo*      belong_ = nullptr;
		git_index* index_  = nullptr;
	};

	// -----------------------------------------------------------------------
	// signature  (author / committer)
	// -----------------------------------------------------------------------

	class signature
	{
	public:
		signature(const signature& other);
		signature(signature&& other) noexcept;
		signature(const std::string& name, const std::string& email);
		~signature() noexcept;

		signature& operator=(const signature& other);
		signature& operator=(signature&& other) noexcept;

		git_signature*       native_handle() noexcept;
		const git_signature* native_handle() const noexcept;

	private:
		git_signature* git_sig_ = nullptr;
	};

	// -----------------------------------------------------------------------
	// remote
	// -----------------------------------------------------------------------

	class remote
	{
	public:
		explicit remote(git_remote* r) noexcept;
		remote(const remote& other);
		remote(remote&& other) noexcept;
		~remote() noexcept;

		remote& operator=(const remote& other);
		remote& operator=(remote&& other) noexcept;

		git_remote* native_handle() noexcept;

		void push(const git_strarray* refspecs, const git_push_options* opts);

	private:
		git_remote* git_remote_ = nullptr;
	};

	// -----------------------------------------------------------------------
	// revwalk  –  traverse revision history
	// -----------------------------------------------------------------------

	class revwalk
	{
	public:
		explicit revwalk(git_repository* repo);
		~revwalk() noexcept;

		revwalk(const revwalk&) = delete;
		revwalk& operator=(const revwalk&) = delete;
		revwalk(revwalk&& other) noexcept;
		revwalk& operator=(revwalk&& other) noexcept;

		void push_head();
		void push_oid(const oid& o);
		void push_ref(const std::string& refname);
		void hide_oid(const oid& o);
		void sorting(unsigned int sort_mode) const noexcept;

		/// Returns the next oid in the walk, or a zero‑filled oid when done.
		oid next();

		git_revwalk* native_handle() noexcept { return walk_; }

	private:
		git_revwalk* walk_ = nullptr;
	};

	// -----------------------------------------------------------------------
	// branch
	// -----------------------------------------------------------------------

	class branch
	{
	public:
		branch() noexcept = default;

		std::string_view name() const noexcept;
		std::string_view shorthand() const noexcept;
		reference       get_reference() const;
		bool            is_head() const noexcept;
		bool            valid() const noexcept { return ref_ != nullptr; }
		explicit operator bool() const noexcept { return valid(); }

	private:
		friend class repo;
		friend class branch_iterator;
		explicit branch(git_reference* ref) noexcept;
		git_reference* ref_ = nullptr;
	};

	class branch_iterator
	{
	public:
		branch_iterator(git_repository* repo, git_branch_t type);
		~branch_iterator() noexcept;

		branch_iterator(const branch_iterator&) = delete;
		branch_iterator& operator=(const branch_iterator&) = delete;
		branch_iterator(branch_iterator&& other) noexcept;
		branch_iterator& operator=(branch_iterator&& other) noexcept;

		/// Returns the next branch, or a default‑constructed (invalid) one.
		branch next();

	private:
		git_branch_iterator* iter_ = nullptr;
	};

	// -----------------------------------------------------------------------
	// tag  –  lightweight annotated tag wrapper
	// -----------------------------------------------------------------------

	class tag
	{
	public:
		tag() noexcept = default;
		explicit tag(git_tag* t) noexcept;
		~tag() noexcept;

		tag(tag&& other) noexcept;
		tag& operator=(tag&& other) noexcept;

		tag(const tag&) = delete;
		tag& operator=(const tag&) = delete;

		std::string_view name() const noexcept;
		std::string_view message() const noexcept;
		oid             target_id() const;
		git_object_t    target_type() const noexcept;

		bool valid() const noexcept { return tag_ != nullptr; }
		explicit operator bool() const noexcept { return valid(); }

		git_tag*       native_handle() noexcept { return tag_; }
		const git_tag* native_handle() const noexcept { return tag_; }

	private:
		git_tag* tag_ = nullptr;
	};

	// -----------------------------------------------------------------------
	// diff
	// -----------------------------------------------------------------------

	class diff
	{
	public:
		diff() noexcept = default;
		explicit diff(git_diff* d) noexcept;
		~diff() noexcept;

		diff(diff&& other) noexcept;
		diff& operator=(diff&& other) noexcept;

		diff(const diff&) = delete;
		diff& operator=(const diff&) = delete;

		std::size_t num_deltas() const noexcept;
		const git_diff_delta* get_delta(std::size_t idx) const noexcept;

		git_diff*       native_handle() noexcept { return diff_; }
		const git_diff* native_handle() const noexcept { return diff_; }

		bool valid() const noexcept { return diff_ != nullptr; }
		explicit operator bool() const noexcept { return valid(); }

	private:
		git_diff* diff_ = nullptr;
	};

	// -----------------------------------------------------------------------
	// repo  –  the main repository handle
	// -----------------------------------------------------------------------

	class repo : boost::noncopyable
	{
	public:
		explicit repo(git_repository* r) noexcept;
		explicit repo(std::filesystem::path repo_dir);   // throws not_repo
		~repo() noexcept;

		repo(repo&& other) noexcept;
		repo& operator=(repo&& other) noexcept;

		git_repository*       native_handle() noexcept { return repo_; }
		const git_repository* native_handle() const noexcept { return repo_; }

		// --- index & status ------------------------------------------------

		index       get_index();
		status_list new_status_list();
		void        index_add_bypath(const std::string& path);

		// --- references ----------------------------------------------------

		reference head() const;
		reference lookup_reference(const std::string& name) const;

		// --- remotes -------------------------------------------------------

		remote get_remote(const std::string& remote_name);

		// --- commits ------------------------------------------------------

		commit  lookup_commit(oid commit_id);
		commit  create_commit(const std::string& update_ref,
		                      const signature& author,
		                      const signature& committer,
		                      const std::string& message,
		                      const tree& tree,
		                      commit parent);
		commit  create_commit(const std::string& update_ref,
		                      const signature& author,
		                      const signature& committer,
		                      const std::string& message,
		                      const tree& tree,
		                      std::vector<commit> parents);

		// --- trees & blobs ------------------------------------------------

		tree get_tree_by_commit(oid commit_id);
		tree get_tree_by_treeid(oid tree_oid);
		tree get_tree_from_reference(oid tree_oid);
		blob get_blob(oid blob_id) const;

		// --- revwalk ------------------------------------------------------

		revwalk new_revwalk();

		// --- branches -----------------------------------------------------

		branch          lookup_branch(const std::string& name,
		                              git_branch_t type = GIT_BRANCH_LOCAL);
		branch_iterator new_branch_iterator(git_branch_t type = GIT_BRANCH_LOCAL);

		// --- tags ---------------------------------------------------------

		tag lookup_tag(oid tag_id);

		// --- diff ---------------------------------------------------------

		diff diff_trees(const tree& old_tree, const tree& new_tree);
		diff diff_tree_to_workdir(const tree& old_tree);
		diff diff_index_to_workdir();

		// --- properties ---------------------------------------------------

		bool is_bare() const noexcept;
		bool is_empty() const;
		bool is_head_detached() const;
		bool is_head_unborn() const;

		std::string path() const;
		std::string workdir() const;

	private:
		git_repository* repo_ = nullptr;

		void check_repo() const;
	};

	// -----------------------------------------------------------------------
	// Free functions
	// -----------------------------------------------------------------------

	bool is_git_repo(std::filesystem::path dir);
	bool init_git_repo(std::filesystem::path repo_path,
	                   const std::string& url = {},
	                   bool bare = false);

} // namespace gitpp
