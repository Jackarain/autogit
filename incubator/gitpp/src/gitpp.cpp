
#include <cstring>
#include "gitpp/gitpp.hpp"
#include "git2/oid.h"

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
	: owned(other.owned), entry(other.entry)
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

gitpp::reference gitpp::repo::head() const
{
	git_reference* out_ref = nullptr;
	git_repository_head(&out_ref, repo_);

	return reference(out_ref);
}

gitpp::tree gitpp::repo::get_tree_by_commit(gitpp::oid commitid)
{
	git_tree* ctree = nullptr;
	git_commit* commit;
	git_commit_lookup(&commit, repo_, & commitid);
	object a_commit( (git_object*) commit);
	git_commit_tree(&ctree, commit);
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


