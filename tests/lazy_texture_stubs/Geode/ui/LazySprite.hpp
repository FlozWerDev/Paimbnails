
#pragma once
#include <functional>
#include <utility>
#include <vector>
#include <filesystem>
namespace cocos2d { struct CCTexture2D {}; }
namespace geode {
struct RefCounted { int refs=1; virtual ~RefCounted()=default; void retain(){++refs;} void release(){if(!--refs)delete this;} };
template<class T> class Ref {
    T* p=nullptr;
public:
    Ref()=default; Ref(T* q):p(q){if(p)p->retain();}
    Ref(Ref const& r):Ref(r.p){} Ref(Ref&& r):p(std::exchange(r.p,nullptr)){}
    ~Ref(){if(p)p->release();}
    Ref& operator=(T* q){if(q)q->retain();if(p)p->release();p=q;return *this;}
    T* operator->()const{return p;} explicit operator bool()const{return p!=nullptr;}
};
template<class T=void> struct Result { bool good=true; explicit operator bool()const{return good;} };
class LazySprite:public RefCounted {
public:
    enum class Format{kFmtUnKnown};
    using Callback=std::function<void(Result<>)>;
    static inline int live=0;
    static inline std::vector<LazySprite*> pool,all;
    Callback callback;
    cocos2d::CCTexture2D texture;
    LazySprite(){++live;} ~LazySprite(){--live;}
    static LazySprite* create(std::pair<float,float>,bool){auto* p=new LazySprite;pool.push_back(p);all.push_back(p);return p;}
    void setLoadCallback(Callback c){callback=std::move(c);}
    void cancelLoad(){} // Geode 5.11.2 only clears loading/spinner, not callback.
    void loadFromFile(std::filesystem::path const&,Format,bool){}
    cocos2d::CCTexture2D* getTexture(){return &texture;}
    void complete(){callback(Result<>{});} // SDK calls member callback without clearing it.
    static void drain(){auto old=std::exchange(pool,{});for(auto* p:old)p->release();}
};
}
