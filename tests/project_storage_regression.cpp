#include "../src/utils/ProjectStorage.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <set>
int main(int argc,char**argv){
 namespace fs=std::filesystem; namespace storage=paimon::storage;
 assert(argc==2); fs::path root=fs::absolute(argv[1]);
 for(auto id:{"", ".", "..", "../victim", "a/b", "a\\b", "C:foo", "CON", "NUL.png", "foo.", "foo ", "COM1"})
  assert(!storage::isSafeProjectId(id));
 for(auto id:{"legacy-icon", "Cube", "cube_2", "pack.v2"})assert(storage::isSafeProjectId(id));
 std::error_code ec;
 auto a=storage::reserveProjectDirectory(root,ec);
 if(ec||a.empty())std::cerr<<"Cannot reserve under "<<root<<": "<<ec.value()<<" "<<ec.message()<<"\n";
 assert(!ec&&!a.empty());
 auto b=storage::reserveProjectDirectory(root,ec);assert(!ec&&!b.empty()&&a!=b);
 assert(storage::isSafeProjectId(a.filename().string()));
 assert(storage::resolveProjectDirectory(root,a.filename().string())==a);
 assert(!storage::resolveProjectDirectory(root,"../../victim"));
 assert(!storage::resolveProjectDirectory(root,".."));
 fs::path outside=root.parent_path()/"outside-fixture";fs::create_directories(outside);
 fs::create_directory_symlink(outside,root/"link",ec);
 if(!ec)assert(!storage::resolveProjectDirectory(root,"link"));
 std::set<std::string> ids;
 for(int i=0;i<1000;++i){auto id=storage::newProjectId();assert(storage::isSafeProjectId(id));assert(ids.insert(id).second);}
 std::cout<<"Project IDs and confined directories verified\n";
}
