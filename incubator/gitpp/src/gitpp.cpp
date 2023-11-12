
#include <cstring>
#include "gitpp/gitpp.hpp"
#include "git2/oid.h"

#include <exception>
#include <algorithm>
#include <git2.h>

namespace gitpp {

	struct auto_init_libgit2 {
		auto_init_libgit2() {
			git_libgit2_init();
		};

		~auto_init_libgit2() {
			git_libgit2_shutdown();
		}
	};

	static auto_init_libgit2 ensure_libgit2_initialized;

	exception::error::error()
		: e_(git_error_last())
	{
	}

	object::object(git_object* o)
		: obj_(o)
	{
	}

	object::object(const object& o)
	{
		git_object_dup(&obj_, (git_object*)o.obj_);
	}

	object::object(object&& o)
		: obj_(o.obj_)
	{
		o.obj_ = nullptr;
	}

	object::~object()
	{
		if (obj_)
			git_object_free(obj_);
	}

	oid object::get_oid() const
	{
		return oid(git_object_id(this->obj_));
	}

	blob::blob(git_blob* b)
		: object((git_object*)b)
	{
	}

	std::string_view blob::get_content() const
	{
		auto content = (const char*)git_blob_rawcontent((git_blob*)obj_);
		const auto content_size = git_blob_rawsize((git_blob*)obj_);

		return std::string_view(content, content_size);
	}

	std::size_t blob::size() const
	{
		const auto content_size = git_blob_rawsize((git_blob*)obj_);
		return content_size;
	}

	commit::commit(git_commit* _git_commit)
		: object((git_object*)_git_commit)
	{}

	git_commit* commit::native_handle()
	{
		return reinterpret_cast<git_commit*> (this->obj_);
	}

	const git_commit* commit::native_handle() const
	{
		return reinterpret_cast<const git_commit*> (this->obj_);
	}

	tree::tree(git_tree* t)
		: object((git_object*)t)
	{
	}

	const git_tree* tree::native_handle() const
	{
		return (const git_tree*)obj_;
	}

	git_tree* tree::native_handle()
	{
		return (git_tree*)obj_;
	}

	tree_entry tree::by_path(std::string path) const
	{
		git_tree_entry* entry2 = nullptr;
		git_tree_entry_bypath(&entry2, native_handle(), path.c_str());
		tree_entry sub_tree_entry(entry2);
		return sub_tree_entry;
	}

	tree::tree_iterator tree::begin() const
	{
		return tree_iterator(this, 0);
	}

	tree::tree_iterator tree::end() const
	{
		return tree_iterator(this, git_tree_entrycount(native_handle()));
	}

	tree::tree_iterator::tree_iterator(const tree* parent, std::size_t index)
		: parent_(parent)
		, index_(index)
	{
	}

	tree::tree_iterator& tree::tree_iterator::operator++()
	{
		++index_;
		return *this;
	}

	tree_entry tree::tree_iterator::operator*()
	{
		return tree_entry(
			git_tree_entry_byindex(
				parent_->native_handle(),
				index_));
	}

	tree_entry::tree_entry(const git_tree_entry* entry)
		: entry_(const_cast<git_tree_entry*>(entry)), owned_(false)
	{

	}

	tree_entry::tree_entry(git_tree_entry* entry)
		: entry_(entry), owned_(true)
	{
	}

	tree_entry::~tree_entry()
	{
		if (owned_ && entry_)
			git_tree_entry_free(entry_);
	}

	tree_entry::tree_entry(tree_entry&& other)
		: entry_(other.entry_)
		, owned_(other.owned_)
	{
		other.owned_ = false;
	}

	tree_entry::tree_entry(const tree_entry& other)
		: entry_(other.entry_)
	{
		owned_ = false;
	}

	bool tree_entry::empty() const
	{
		return entry_ == nullptr;
	}

	oid tree_entry::get_oid() const
	{
		return oid(git_tree_entry_id(entry_));
	}

	git_object_t tree_entry::type() const
	{
		return git_tree_entry_type(entry_);
	}

	std::string tree_entry::name() const
	{
		return git_tree_entry_name(entry_);
	}


	repo::repo(git_repository* repo_) noexcept
		: repo_(repo_)
	{
	}

	repo::repo(std::filesystem::path repo_dir)
		: repo_(nullptr)
	{
		if (git_repository_open_ext(&repo_,
			repo_dir.string().c_str(),
			GIT_REPOSITORY_OPEN_NO_SEARCH,
			nullptr) != 0)
		{
			throw exception::not_repo();
		}
	}

	repo::~repo() noexcept
	{
		if (repo_)
			git_repository_free(repo_);
	}

	bool repo::is_bare() const noexcept
	{
		return git_repository_is_bare(repo_);
	}

	bool is_git_repo(std::filesystem::path dir)
	{
		try
		{
			repo tmp(dir);
			tmp.head().target();
			return true;
		}
		catch (exception::git2_exception&)
		{
		}

		return false;
	}

	bool oid::operator==(const oid& other) const
	{
		return memcmp(&oid_, &other.oid_, sizeof(oid_)) == 0;
	}

	oid& oid::operator = (const oid& other)
	{
		memcpy(&oid_, &other.oid_, sizeof(oid_));
		return *this;
	}

	std::string oid::as_sha1_string() const
	{
		std::string ret;
		ret.resize(GIT_OID_HEXSZ);
		git_oid_tostr(ret.data(), ret.size() + 1, &oid_);

		return ret;
	}

	oid oid::from_sha1_string(std::string_view s)
	{
		git_oid out;
		git_oid_fromstrn(&out, s.data(), s.length());

		return oid(&out);
	}

	status_list::status_list(git_status_list* _status_list)
		: status_list_(_status_list)
	{}

	status_list::~status_list()
	{
		git_status_list_free(status_list_);
	}

	git_status_list* status_list::native_handle()
	{
		return status_list_;
	}

	const git_status_list* status_list::native_handle() const
	{
		return status_list_;
	}

	status_list::git_status_entry_iterator::git_status_entry_iterator(
		status_list& parent, std::size_t idx)
		: parent_(parent)
		, idx_(idx)
	{}

	const git_status_entry* status_list::git_status_entry_iterator::operator*()
	{
		return git_status_byindex(parent_.native_handle(), idx_);
	}

	status_list::git_status_entry_iterator&
	status_list::git_status_entry_iterator::operator++()
	{
		idx_++;
		return *this;
	}

	bool status_list::git_status_entry_iterator::operator==(
		const git_status_entry_iterator& other) const
	{
		return (&parent_ == &other.parent_) && (this->idx_ == other.idx_);
	}

	status_list::git_status_entry_iterator status_list::begin()
	{
		return status_list::git_status_entry_iterator(*this, 0);
	}

	status_list::git_status_entry_iterator status_list::end()
	{
		return status_list::git_status_entry_iterator(*this, size());
	}

	std::size_t status_list::size() const
	{
		return git_status_list_entrycount(status_list_);
	}

	index::index(repo* belong, git_index* i)
		: belong_(belong)
		, index_(i)
	{
	}

	index::~index()
	{
		git_index_free(index_);
	}

	git_index* index::native_handle()
	{
		return index_;
	}

	tree index::write_tree()
	{
		oid tree_id;
		if (git_index_write_tree(&tree_id, native_handle()) != 0)
		{
			throw exception::error();
		}

		return belong_->get_tree_by_treeid(tree_id);
	}

	remote::remote(git_remote* _r)
		: git_remote_(_r)
	{}

	remote::remote(const remote& other)
	{
		git_remote_dup(&git_remote_, other.git_remote_);
	}

	remote::remote(remote&& other)
		: git_remote_(other.git_remote_)
	{
		other.git_remote_ = nullptr;
	}

	remote::~remote()
	{
		if (git_remote_)
			git_remote_free(git_remote_);
	}

	remote& remote::operator=(const remote& other)
	{
		if (git_remote_)
			git_remote_free(git_remote_);
		git_remote_dup(&git_remote_, other.git_remote_);

		return *this;
	}

	remote& remote::operator=(remote&& other)
	{
		if (git_remote_)
			git_remote_free(git_remote_);
		git_remote_ = other.git_remote_;
		other.git_remote_ = nullptr;

		return *this;
	}

	git_remote* remote::native_handle()
	{
		return git_remote_;
	}

	index repo::get_index()
	{
		git_index* gi = nullptr;
		git_repository_index(&gi, native_handle());

		return index(this, gi);
	}

	status_list repo::new_status_list()
	{
		git_status_list* _status_list;
		git_status_list_new(&_status_list, native_handle(), nullptr);

		return status_list(_status_list);
	}

	reference repo::head() const
	{
		git_reference* out_ref = nullptr;
		git_repository_head(&out_ref, repo_);

		return reference(out_ref);
	}

	remote repo::get_remote(const std::string& name)
	{
		git_remote* gr;
		if (git_remote_lookup(&gr, native_handle(), name.c_str()) != 0)
		{
			throw exception::error();
		}

		return remote(gr);
	}

	commit repo::lookup_commit(oid commitid)
	{
		git_commit* r = nullptr;
		if (git_commit_lookup(&r, native_handle(), &commitid) != 0)
		{
			throw exception::error();
		}

		return commit(r);
	}

	tree repo::get_tree_by_commit(oid commitid)
	{
		git_tree* ctree = nullptr;
		git_commit* commit;
		git_commit_lookup(&commit, repo_, &commitid);
		object a_commit((git_object*)commit);
		git_commit_tree(&ctree, commit);
		git_commit_free(commit);

		return tree(ctree);
	}

	tree repo::get_tree_by_treeid(oid tree_oid)
	{
		git_tree* ctree = nullptr;
		git_tree_lookup(&ctree, repo_, &tree_oid);
		return tree(ctree);
	}

	blob repo::get_blob(oid blob_id) const
	{
		git_blob* cblob = nullptr;
		git_blob_lookup(&cblob, repo_, &blob_id);

		return blob(cblob);
	}

	bool init_git_repo(std::filesystem::path repo_path,
		const std::string& url/* = ""*/, bool bare/* = false*/)
	{
		git_repository_init_options opts = GIT_REPOSITORY_INIT_OPTIONS_INIT;

		opts.flags = GIT_REPOSITORY_INIT_MKPATH; /* don't love this default */
		if (bare)
			opts.flags |= GIT_REPOSITORY_INIT_BARE;

		opts.origin_url = url.empty() ? nullptr : url.c_str();
		opts.initial_head = "master";

		git_repository* repo = nullptr;
		int ret = git_repository_init_ext(&repo, repo_path.string().c_str(), &opts);
		git_repository_free(repo);

		return ret == 0;
	}

	reference::reference(git_reference* ref)
		: ref_(ref), owned_(true)
	{
	}

	bool reference::operator==(const reference& other) const
	{
		return this->ref_ == other.ref_;
	}

	reference::reference(const git_reference* ref)
		: ref_(const_cast<git_reference*>(ref)), owned_(false)
	{
	}

	reference::reference(const reference& other)
		: ref_(nullptr), owned_(true)
	{
		git_reference_dup(&ref_, other.ref_);
	}

	reference::reference(reference&& other)
		: ref_(other.ref_), owned_(other.owned_)
	{
		other.owned_ = false;
	}

	reference::~reference()
	{
		if (owned_)
			git_reference_free(ref_);
	}

	git_reference_t reference::type() const
	{
		return git_reference_type(ref_);
	}

	reference reference::resolve() const
	{
		git_reference* out = nullptr;
		if (0 == git_reference_resolve(&out, ref_))
			return reference(out);
		throw exception::resolve_failed();
	}

	oid reference::target() const
	{
		if (type() == GIT_REFERENCE_DIRECT)
		{
			const git_oid* tgt = git_reference_target(ref_);
			if (tgt == nullptr)
				throw exception::resolve_failed{};
			return oid(tgt);
		}

		return resolve().target();
	}

#if 0
	buf::buf(git_buf* b) noexcept
	{
		memcpy(&buf_, b, sizeof buf_);
	}

	buf::buf() noexcept
	{
		memset(&buf_, 0, sizeof buf_);
	}

	buf::~buf() noexcept
	{
		git_buf_dispose(&buf_);
	}

	buf::operator std::string_view() noexcept
	{
		return std::string_view(buf_.ptr, buf_.size);
	}
#endif

	signature::signature(const signature& other)
	{
		git_signature_dup(&git_sig_, other.native_handle());
	}

	signature::signature(signature&& other)
		: git_sig_(other.git_sig_)
	{
		other.git_sig_ = nullptr;
	}

	signature::signature(const std::string& name, const std::string& email)
	{
		if (git_signature_now(&git_sig_, name.c_str(), email.c_str()) != 0)
		{
			throw exception::error();
		}
	}

	git_signature* signature::native_handle()
	{
		return git_sig_;
	}

	const git_signature* signature::native_handle() const
	{
		return git_sig_;
	}

	signature::~signature()
	{
		if (git_sig_)
			git_signature_free(git_sig_);
	}

	signature& signature::operator = (const signature& other)
	{
		git_signature_free(git_sig_);
		git_signature_dup(&git_sig_, other.native_handle());

		return *this;
	}

	signature& signature::operator = (signature&& other)
	{
		if (git_sig_)
			git_signature_free(git_sig_);
		git_sig_ = other.git_sig_;
		other.git_sig_ = nullptr;

		return *this;
	}

	commit repo::create_commit(
		const std::string& update_ref,
		const signature& author,
		const signature& committer,
		const std::string& message,
		const tree& tree,
		commit parent)
	{
		oid commit_id;
		const git_commit* parents_array[1] = { parent.native_handle() };

		if (git_commit_create(
			&commit_id,
			this->native_handle(),
			update_ref.c_str(),
			author.native_handle(),
			committer.native_handle(),
			nullptr,
			message.c_str(),
			tree.native_handle(),
			1,
			parents_array) != 0)
		{
			throw exception::error();
		}

		return lookup_commit(commit_id);
	}

	commit repo::create_commit(
		const std::string& update_ref,
		const signature& author,
		const signature& committer,
		const std::string& message,
		const tree& tree,
		std::vector<commit> parents)
	{
		oid commit_id;
		std::vector<const git_commit*> parents_array;
		parents_array.reserve(parents.size());

		std::transform(parents.begin(),
			parents.end(),
			std::back_inserter(parents_array),
			[](commit& p) { return p.native_handle(); }
		);

		if (git_commit_create(
			&commit_id,
			this->native_handle(),
			update_ref.c_str(),
			author.native_handle(),
			committer.native_handle(),
			nullptr,
			message.c_str(),
			tree.native_handle(),
			parents.size(),
			parents_array.data()) != 0)
		{
			throw exception::error();
		}

		return lookup_commit(commit_id);
	}
}
