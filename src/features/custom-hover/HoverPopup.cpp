#include "CustomHover.hpp"
#include "../../ui/PaiConfigKit.hpp"
#include "../../utils/DynamicPopupRegistry.hpp"
#include "../../core/modules/ModuleRegistry.hpp"
#include "../../core/Settings.hpp"
#include "../icon-copy/ui/IconSetNamePopup.hpp"
using namespace geode::prelude;
namespace paimon::hover {
namespace {
namespace kit=paimon::configkit;
class HoverPopup;
HoverPopup* active=nullptr;
class HoverPopup : public Popup {
    std::string key;
    int tab=0;
    ScrollLayer* scroll=nullptr;
    CCNode* preview=nullptr;
    float elapsed=0;
    bool pending=false;
    Config config() { return Manager::get().resolve(key); }
    void changed(Config c) { Manager::get().set(key,c); elapsed=0; }
    void rebuildLater() {
        if(pending) return; pending=true;
        Ref<HoverPopup> self=this;
        Loader::get()->queueInMainThread([self]{self->pending=false; if(self->getParent()) self->rebuild();});
    }
    CCNode* slider(char const* name, float Config::*field, float lo, float hi, char const* unit="") {
        return kit::makeSliderRow(356,name,nullptr,config().*field,lo,hi,
            [unit](double v){return fmt::format("{:.2f}{}",v,unit);},
            [this,field](double v){auto c=config(); c.*field=static_cast<float>(v); changed(c);});
    }
    void rebuild() {
        if(scroll) { scroll->removeFromParent(); scroll=nullptr; }
        auto& m=Manager::get(); auto c=config();
        std::vector<CCNode*> rows;
        rows.push_back(kit::makeHint(376,key.empty()?"Configuracion global. Ctrl+clic: editar un boton.":"Boton seleccionado. Los cambios se ven al instante."));
        rows.push_back(kit::makeToggleRow(376,"Custom Hover","Cursor en PC; deslizar el dedo en movil.",
            paimon::modules::isEnabled("paimbnails.customhover.global"),[](bool v){paimon::modules::setEnabled("paimbnails.customhover.global",v);}));
        rows.push_back(kit::makeTabBar(376,{"Presets","Ajustes","Guardados"},tab,[this](int v){tab=v; rebuildLater();}));
        if(tab==0) {
            rows.push_back(kit::makeCard(376,"Aplicar a",{120,210,255},{
                kit::makeToggleRow(356,"Todos vinculados","Ctrl+D: usa este estilo en todos. Desactiva para recuperar los individuales.",m.linked,[this](bool v){
                    auto& m=Manager::get(); if(v) m.group(config()); else {m.linked=false; m.save();} rebuildLater();}),
                kit::makeButtonRow(356,"Elegir boton","Cierra esta ventana y toca el boton que quieras editar.","Elegir",[this]{Manager::get().picking=true; onClose(nullptr);
                    Notification::create("Toca un boton para editar su hover",NotificationIcon::Info)->show();}),
                kit::makeButtonRow(356,"Cancelar seleccion","Desactiva el modo Elegir boton.","Cancelar",[]{Manager::get().picking=false;})
            }));
            std::vector<CCNode*> choices;
            for(auto const& p:presets()) choices.push_back(kit::makeButtonRow(356,p.name.c_str(),nullptr,"Usar",[this,c=p.config]{changed(c); rebuildLater();}));
            rows.push_back(kit::makeCard(376,"24 estilos listos para usar",{255,210,100},choices));
        } else if(tab==1) {
            rows.push_back(kit::makeCard(376,"Forma y movimiento",{120,210,255},{
                kit::makeToggleRow(356,"Animar este estilo",nullptr,c.enabled,[this](bool v){auto c=config(); c.enabled=v; changed(c);}),
                slider("Escala",&Config::scale,.5f,1.8f,"x"),slider("Ancho / alto",&Config::stretch,.6f,1.5f,"x"),
                slider("Desplazamiento vertical",&Config::lift,-25,25),slider("Desplazamiento horizontal",&Config::slide,-25,25),
                slider("Rotacion",&Config::rotation,-180,180," deg")
            }));
            rows.push_back(kit::makeCard(376,"Tiempo",{255,200,100},{
                kit::makeSelectRow(356,"Curva",nullptr,{"Lineal","Suave","Rapida","Rebote","Elastica"},c.easing,[this](int v){auto c=config(); c.easing=v; changed(c);}),
                slider("Entrada",&Config::enter,.05f,1.5f,"s"),slider("Salida",&Config::exit,.05f,1.5f,"s"),slider("Espera al entrar",&Config::delay,0,1,"s")
            }));
            rows.push_back(kit::makeCard(376,"Mientras esta encima",{220,150,255},{
                kit::makeSelectRow(356,"Movimiento continuo",nullptr,{"Ninguno","Latido","Flotar","Balanceo","Gelatina"},c.loop,[this](int v){auto c=config(); c.loop=v; changed(c);}),
                slider("Intensidad",&Config::amplitude,0,.25f),slider("Frecuencia",&Config::frequency,.2f,5," Hz")
            }));
            rows.push_back(kit::makeButtonRow(376,"Restaurar estilo","Vuelve al hover suave.","Restaurar",[this]{changed(Config{}); rebuildLater();}));
            if(!key.empty() && !m.linked) rows.push_back(kit::makeButtonRow(376,"Heredar global","Elimina el ajuste de este boton.","Heredar",[this]{Manager::get().buttons.erase(key); Manager::get().save(); rebuildLater();}));
        } else {
            rows.push_back(kit::makeButtonRow(376,"Guardar preset","Hasta 100 presets. El mismo nombre actualiza el existente.","Guardar",[this]{
                WeakRef<HoverPopup> self=this;
                auto snapshot=config();
                auto* popup=paimon::iconcopy::IconSetNamePopup::create("Guardar hover","",[self,snapshot](std::string const& name){
                    auto& m=Manager::get(); if(m.saved.size()>=100 && !m.saved.count(name)) {
                        Notification::create("Limite de 100 presets",NotificationIcon::Warning)->show(); return;
                    }
                    m.saved[name]=snapshot; m.save(); if(auto p=self.lock()) p->rebuildLater();
                });
                if(popup) kit::showAbove(popup,this);
            }));
            if(m.saved.empty()) rows.push_back(kit::makeHint(376,"Ajusta un estilo y guardalo con tu propio nombre."));
            for(auto const& [name,saved]:m.saved) {
                rows.push_back(kit::makeCard(376,name.c_str(),{140,240,170},{
                    kit::makeButtonRow(356,"Aplicar preset",nullptr,"Usar",[this,saved]{changed(saved); rebuildLater();}),
                    kit::makeButtonRow(356,"Eliminar preset",nullptr,"Borrar",[this,name]{
                        createQuickPopup("Eliminar preset",fmt::format("Eliminar <cy>{}</c>?",name),"Cancelar","Borrar",[self=WeakRef<HoverPopup>(this),name](auto*,bool yes){
                            if(!yes) return; Manager::get().saved.erase(name); Manager::get().save(); if(auto p=self.lock()) p->rebuildLater();
                        });
                    })
                }));
            }
        }
        rows.push_back(kit::makeHint(376,"Vista automatica arriba. Respeta Movimiento reducido. No afecta los controles del nivel ni el lienzo del editor."));
        scroll=kit::makeScrollStack({376,205},rows); scroll->setPosition({12,20}); m_mainLayer->addChild(scroll);
    }
    bool init(std::string const& target) {
        if(!Popup::init(400,290)) return false;
        key=target; setID("custom-hover-popup"); paimon::markDynamicPopup(this); setTitle("Custom Hover");
        auto* spr=ButtonSprite::create("Hover","bigFont.fnt","GJ_button_01.png",.7f);
        auto* btn=CCMenuItemExt::createSpriteExtra(spr,[this](auto*){elapsed=0;});
        btn->setScale(.55f); btn->setPosition({325,250}); m_buttonMenu->addChild(btn); preview=spr;
        auto* caption=CCLabelBMFont::create("Vista previa", "chatFont.fnt"); caption->setScale(.45f); caption->setPosition({325,228}); m_mainLayer->addChild(caption);
        rebuild(); scheduleUpdate(); active=this; return true;
    }
    void update(float dt) override {
        elapsed+=std::clamp(dt,0.f,.05f); auto c=config();
        float cycle=std::fmod(elapsed,c.delay+c.enter+1.3f+c.exit+.6f);
        float amount=0;
        if(cycle>c.delay && cycle<c.delay+c.enter) amount=ease((cycle-c.delay)/c.enter,c.easing);
        else if(cycle>=c.delay+c.enter && cycle<c.delay+c.enter+1.3f) amount=1;
        else if(cycle>=c.delay+c.enter+1.3f) amount=1-ease((cycle-c.delay-c.enter-1.3f)/c.exit,1);
        if(!c.enabled || paimon::settings::smoothui::reducedMotion()) amount=0;
        auto p=pose(c,amount,elapsed);
        preview->setScaleX(p.sx); preview->setScaleY(p.sy); preview->setRotation(p.rotation);
        auto size=preview->getParent()->getContentSize(); preview->setPosition({size.width/2+p.x,size.height/2+p.y});
    }
    void onExit() override { if(active==this) active=nullptr; Popup::onExit(); }
public:
    void groupAll() { Manager::get().group(config()); rebuildLater(); Notification::create("Hover: todos vinculados",NotificationIcon::Success)->show(); }
    static HoverPopup* create(std::string const& key) { auto p=new HoverPopup(); if(p->init(key)){p->autorelease();return p;} delete p; return nullptr; }
};
}
bool popupOpen(){return active!=nullptr;}
void groupFromPopup(){if(active)active->groupAll();}
void open(std::string key){if(!active)if(auto p=HoverPopup::create(key))p->show();}
}
