
#include <cassert>
#include <iostream>
#include "../src/utils/RetainedLazyTextureLoad.hpp"
int main(){
    using geode::LazySprite;
    auto owner=std::make_shared<int>(42); std::weak_ptr<int> weak=owner;
    for(int i=0;i<25;++i){
        paimon::image::RetainedLazyTextureLoad load;
        load.loadFromFile("fixture.png",[owner](auto*,bool){});
        LazySprite::all.back()->complete();
        LazySprite::drain();
    }
    owner.reset();
    assert(LazySprite::live==0); assert(weak.expired());
    std::cout<<"25 completed/destroyed loaders left "<<LazySprite::live<<" sprites; callback context released.\n";
    {paimon::image::RetainedLazyTextureLoad cancelled;cancelled.loadFromFile("pending.png",[](auto*,bool){});}
    LazySprite::drain(); assert(LazySprite::live==0);
    std::cout<<"Cancel/reset releases the pending loader.\n";
    { paimon::image::RetainedLazyTextureLoad load;
      int calls=0;
      load.loadFromFile("first.png",[&](auto*,bool){++calls;load.loadFromFile("second.png",[&](auto*,bool){++calls;});});
      LazySprite::all.back()->complete(); LazySprite::all.back()->complete();
      LazySprite::drain(); assert(calls==2);
    }
    assert(LazySprite::live==0);
}
