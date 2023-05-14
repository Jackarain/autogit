
#include <cstring>
#include "gitpp/gitpp.hpp"
#include "git2/oid.h"

#include <exception>
#include <git2.h>

struct auto_init_libgit2 {
	auto_init_libgit2() {
		git_libgit2_init();
	};
	~auto_init_libgit2() {
		git_libgit2_shutdown();
	}
};

static auto_init_libgit2 ensure_libgit2_initialized;

gitpp::exception::error::error()
	: _e(git_error_last())
{
}

gitpp::object::object(git_object* o)
  : obj_(o)
{
}

gitpp::object::object(const gitpp::object& o)
{
	git_object_dup(&obj_, (git_object*) o.obj_);
}

gitpp::object::object(gitpp::object&& o)
	: obj_(o.obj_)
{
	o.obj_ = nullptr;
}

gitpp::object::~object()
{
	if (obj_)
		git_object_free(obj_);
}

gitpp::oid gitpp::object::get_oid() const
{
	return gitpp::oid( git_object_id(this->obj_) );
}

gitpp::blob::blob(git_blob* b)
	: object((git_object*) b)
{
}

std::string_view gitpp::blob::get_content() const
{
	const char * content = (const char*) git_blob_rawcontent((git_blob*)obj_);
	const auto content_size = git_blob_rawsize((git_blob*)obj_);
	return std::string_view(content, content_size);
}

std::size_t gitpp::blob::size() const
{
	const auto content_size = git_blob_rawsize((git_blob*)obj_);
	return content_size;
}

gitpp::commit::commit(git_commit* _git_commit)
	: object( (git_object*) _git_commit)
{}

git_commit* gitpp::commit::native_handle()
{
	return reinterpret_cast<git_commit*> ( this->obj_ );
}

const git_commit* gitpp::commit::native_handle() const
{
	return reinterpret_cast<const git_commit*> ( this->obj_ );
}

gitpp::tree::tree(git_tree* t)
	: object((git_object*) t)
{
}

const git_tree* gitpp::tree::native_handle() const
{
	return (const git_tree*) obj_;
}

git_tree* gitpp::tree::native_handle()
{
	return (git_tree*) obj_;
}

gitpp::tree_entry gitpp::tree::by_path(std::string path) const
{
	git_tree_entry* entry2 = nullptr;
	git_tree_entry_bypath(&entry2, native_handle(), path.c_str());
	gitpp::tree_entry sub_tree_entry(entry2);
	return sub_tree_entry;
}

gitpp::tree::tree_iterator gitpp::tree::begin() const
{
	return tree_iterator(this, 0);
}

gitpp::tree::tree_iterator gitpp::tree::end() const
{
	return tree_iterator(this, git_tree_entrycount(native_handle()));
}

gitpp::tree::tree_iterator::tree_iterator(const tree* parent, std::size_t index)
	: parent(parent)
	, index(index)
{
}

gitpp::tree::tree_iterator& gitpp::tree::tree_iterator::operator++()
{
	++ index;
	return *this;
}

gitpp::tree_entry gitpp::tree::tree_iterator::operator*()
{
	return tree_entry(git_tree_entry_byindex(parent->native_handle(), index));
}

gitpp::tree_entry::tree_entry(const git_tree_entry* entry)
  : entry(const_cast<git_tree_entry*>(entry)), owned(false)
{

}

gitpp::tree_entry::tree_entry(git_tree_entry* entry)
  : entry(entry), owned(true)
{
}

gitpp::tree_entry::~tree_entry()
{
	if (owned && entry)
		git_tree_entry_free(entry);
}

gitpp::tree_entry::tree_entry(tree_entry&& other)
	: entry(other.entry)
	, owned(other.owned)
{
	other.owned = false;
}

gitpp::tree_entry::tree_entry(const tree_entry& other)
	: entry(other.entry)
{
	owned = false;
}

bool gitpp::tree_entry::empty() const
{
	return entry == nullptr;
}

gitpp::oid gitpp::tree_entry::get_oid() const
{
	return oid(git_tree_entry_id(entry));
}

git_object_t gitpp::tree_entry::type() const
{
	return git_tree_entry_type(entry);
}

std::string gitpp::tree_entry::name() const
{
	return git_tree_entry_name(entry);
}


gitpp::repo::repo(git_repository* repo_) noexcept
	: repo_(repo_)
{
}

gitpp::repo::repo(std::filesystem::path repo_dir)
	: repo_(nullptr)
{
	if (0 != git_repository_open_ext(&repo_, repo_dir.string().c_str(), GIT_REPOSITORY_OPEN_NO_SEARCH, nullptr))
	{
		throw exception::not_repo();
	}
}

gitpp::repo::~repo() noexcept
{
	if (repo_)
		git_repository_free(repo_);
}

bool gitpp::repo::is_bare() const noexcept
{
	return git_repository_is_bare(repo_);
}

bool gitpp::is_git_repo(std::filesystem::path dir)
{
	try
	{
		repo tmp(dir);
		tmp.head().target();
		return true;
	}
	catch (exception::git2_exception&)
	{}
	return false;
}

bool gitpp::oid::operator==(const oid& other) const
{
	return memcmp(&oid_, &other.oid_, sizeof (oid_)) == 0;
}

gitpp::oid& gitpp::oid::operator = (const oid& other)
{
	memcpy(&oid_, &other.oid_, sizeof (oid_));
	return *this;
}

std::string gitpp::oid::as_sha1_string() const
{
	std::string ret;
	ret.resize(GIT_OID_HEXSZ);
	git_oid_tostr(ret.data(), ret.size()+1, &oid_);
	return ret;
}

gitpp::oid gitpp::oid::from_sha1_string(std::string_view s)
{
	git_oid out;
	git_oid_fromstrn(&out, s.data(), s.length());
	return gitpp::oid(&out);
}

gitpp::status_list::status_list(git_status_list* _status_list)
	: _status_list(_status_list)
{}

gitpp::status_list::~status_list()
{
	git_status_list_free(_status_list);
}

git_status_list* gitpp::status_list::native_handle()
{
	return _status_list;
}

const git_status_list* gitpp::status_list::native_handle() const
{
	return _status_list;
}

gitpp::status_list::git_status_entry_iterator::git_status_entry_iterator(status_list& parent, std::size_t idx)
	: parent(parent)
	, idx(idx)
{}

const git_status_entry* gitpp::status_list::git_status_entry_iterator::operator*()
{
	return git_status_byindex(parent.native_handle(), idx);
}

 gitpp::status_list::git_status_entry_iterator& gitpp::status_list::git_status_entry_iterator::operator++()
{
	idx ++;
	return *this;
}

bool gitpp::status_list::git_status_entry_iterator::operator==(const git_status_entry_iterator & other) const
{
	return (&parent == &other.parent) && (this->idx == other.idx);
}

gitpp::status_list::git_status_entry_iterator gitpp::status_list::begin()
{
	return gitpp::status_list::git_status_entry_iterator(*this, 0);
}

gitpp::status_list::git_status_entry_iterator gitpp::status_list::end()
{
	return gitpp::status_list::git_status_entry_iterator(*this, size());
}

std::size_t gitpp::status_list::size() const
{
	return git_status_list_entrycount(_status_list);
}

gitpp::index::index(gitpp::repo* belong, git_index* i)
	: belong(belong)
	, _index(i)
{
}

gitpp::index::~index()
{
	git_index_free(_index);
}

git_index* gitpp::index::native_handle()
{
	return _index;
}

gitpp::tree gitpp::index::write_tree()
{
	gitpp::oid tree_id;
	if (git_index_write_tree(&tree_id, native_handle()) != 0)
	{
		throw gitpp::exception::error();
	}

	return belong->get_tree_by_treeid(tree_id);
}

gitpp::index gitpp::repo::get_index()
{
	git_index * _index = nullptr;
	git_repository_index(&_index, native_handle());
	return gitpp::index(this, _index);
}

gitpp::status_list gitpp::repo::new_status_list()
{
	git_status_list * _status_list;
	git_status_list_new(&_status_list, native_handle(), nullptr);
	return gitpp::status_list(_status_list);
}

gitpp::reference gitpp::repo::head() const
{
	git_reference* out_ref = nullptr;
	git_repository_head(&out_ref, repo_);

	return reference(out_ref);
}

gitpp::commit gitpp::repo::lookup_commit(oid commitid)
{
	git_commit* r = nullptr;
	if (git_commit_lookup(&r, native_handle(), &commitid) != 0)
	{
		throw gitpp::exception::error();
	}
	return gitpp::commit(r);
}

gitpp::tree gitpp::repo::get_tree_by_commit(gitpp::oid commitid)
{
	git_tree* ctree = nullptr;
	git_commit* commit;
	git_commit_lookup(&commit, repo_, & commitid);
	object a_commit( (git_object*) commit);
	git_commit_tree(&ctree, commit);
	git_commit_free(commit);
	return tree(ctree);
}

gitpp::tree gitpp::repo::get_tree_by_treeid(gitpp::oid tree_oid)
{
	git_tree* ctree = nullptr;
	git_tree_lookup(&ctree, repo_, & tree_oid);
	return tree(ctree);
}

gitpp::blob gitpp::repo::get_blob(gitpp::oid blob_id) const
{
	git_blob* cblob = nullptr;
	git_blob_lookup(&cblob, repo_, & blob_id);
	return blob(cblob);
}

bool gitpp::init_bare_repo(std::filesystem::path repo_path)
{
	git_repository * repo = nullptr;

	int lg_ret = git_repository_init(&repo, repo_path.string().c_str(), true);
	git_repository_free(repo);

	return lg_ret == 0;

}

gitpp::reference::reference(git_reference* ref)
	: ref(ref), owned(true)
{
}

gitpp::reference::reference(const git_reference* ref)
	: ref(const_cast<git_reference*>(ref)), owned(false)
{
}

gitpp::reference::reference(const gitpp::reference& other)
	: ref(nullptr), owned(true)
{
	git_reference_dup(&ref, other.ref);
}

gitpp::reference::reference(gitpp::reference&& other)
	: ref(other.ref), owned(other.owned)
{
	other.owned = false;
}

gitpp::reference::~reference()
{
	if (owned)
		git_reference_free(ref);
}

git_reference_t gitpp::reference::type() const
{
	return git_reference_type(ref);
}

gitpp::reference gitpp::reference::resolve() const
{
	git_reference* out = nullptr;
	if (0 == git_reference_resolve(&out, ref))
		return reference(out);
	throw exception::resolve_failed();
}

gitpp::oid gitpp::reference::target() const
{
	if (type() == GIT_REFERENCE_DIRECT)
	{
		const git_oid* tgt = git_reference_target(ref);
		if (tgt == nullptr)
			throw exception::resolve_failed{};
		return oid(tgt);
	}
	return resolve().target();
}

gitpp::buf::buf(git_buf* b) noexcept
{
	memcpy(&buf_, b, sizeof buf_);
}

gitpp::buf::buf() noexcept
{
	memset(&buf_, 0, sizeof buf_);
}

gitpp::buf::~buf() noexcept
{
	git_buf_dispose(&buf_);
}

gitpp::buf::operator std::string_view() noexcept
{
	return std::string_view(buf_.ptr, buf_.size);
}

gitpp::signature::signature(const gitpp::signature& other)
{
	git_signature_dup(&_git_sig, other.native_handle());
}

gitpp::signature::signature(gitpp::signature&& other)
	: _git_sig(other._git_sig)
{
	other._git_sig = nullptr;
}

gitpp::signature::signature(const std::string& name,  const std::string& email)
{
	if (git_signature_now(&_git_sig, name.c_str(), email.c_str()) != 0)
	{
		throw gitpp::exception::error();
	}
}

git_signature* gitpp::signature::native_handle()
{
	return _git_sig;
}

const git_signature* gitpp::signature::native_handle() const
{
	return _git_sig;
}

gitpp::signature::~signature()
{
	if (_git_sig)
		git_signature_free(_git_sig);
}

gitpp::signature& gitpp::signature::operator = (const signature& other)
{
	git_signature_free(_git_sig);
	git_signature_dup(&_git_sig, other.native_handle());
	return *this;
}

gitpp::signature& gitpp::signature::operator = (signature&& other)
{
	if (_git_sig)
		git_signature_free(_git_sig);
	_git_sig = other._git_sig;
	other._git_sig = nullptr;
	return *this;
}

gitpp::commit gitpp::repo::create_commit(const std::string& update_ref, const gitpp::signature& author, const gitpp::signature& committer
	, const std::string& message, const gitpp::tree& tree, gitpp::commit parent)
{
	gitpp::oid commit_id;
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
		throw gitpp::exception::error();
	}

	return lookup_commit(commit_id);}

gitpp::commit gitpp::repo::create_commit(const std::string& update_ref, const gitpp::signature& author, const gitpp::signature& committer
	, const std::string& message, const gitpp::tree& tree, std::vector<gitpp::commit> parents)
{
	gitpp::oid commit_id;
	std::vector<const git_commit*> parents_array;
	parents_array.reserve(parents.size());

	std::transform(parents.begin(), parents.end(), std::back_inserter(parents_array), [](gitpp::commit& p){ return p.native_handle();});

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
		throw gitpp::exception::error();
	}

	return lookup_commit(commit_id);
}
