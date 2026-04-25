#include "ProfileReviewsPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../emotes/EmoteRenderer.hpp"
#include "../../emotes/services/EmoteService.hpp"
#include <Geode/binding/GameManager.hpp>

using namespace geode::prelude;

ProfileReviewsPopup* ProfileReviewsPopup::create(int accountID) {
    auto ret = new ProfileReviewsPopup();
    if (ret && ret->init(accountID)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProfileReviewsPopup::init(int accountID) {
    if (!Popup::init(400.f, 290.f)) return false;

    m_accountID = accountID;
    this->setTitle("Profile Reviews");

    auto contentSize = m_mainLayer->getContentSize();
    float centerX = contentSize.width / 2.f;

    // Panel de promedio
    auto headerPanel = paimon::SpriteHelper::createDarkPanel(260.f, 50.f, 90, 8.f);
    headerPanel->setPosition({centerX - 130.f, contentSize.height - 72.f});
    m_mainLayer->addChild(headerPanel, 1);

    // Icono de estrella
    if (auto starIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
        starIcon->setScale(0.6f);
        starIcon->setPosition({centerX - 65.f, contentSize.height - 42.f});
        starIcon->setColor({255, 215, 0});
        m_mainLayer->addChild(starIcon, 2);
    }

    m_averageLabel = CCLabelBMFont::create("...", "bigFont.fnt");
    m_averageLabel->setScale(0.55f);
    m_averageLabel->setPosition({centerX - 15.f, contentSize.height - 40.f});
    m_averageLabel->setColor({255, 215, 0});
    m_mainLayer->addChild(m_averageLabel, 2);

    m_countLabel = CCLabelBMFont::create("Loading...", "chatFont.fnt");
    m_countLabel->setScale(0.55f);
    m_countLabel->setPosition({centerX, contentSize.height - 58.f});
    m_countLabel->setColor({180, 180, 180});
    m_mainLayer->addChild(m_countLabel, 2);

    // Separador
    auto separator = paimon::SpriteHelper::createDarkPanel(320.f, 1.5f, 60, 0.f);
    separator->setPosition({centerX - 160.f, contentSize.height - 78.f});
    m_mainLayer->addChild(separator, 1);

    // Spinner de carga
    m_spinner = PaimonLoadingOverlay::create("Loading...", 35.f);
    m_spinner->show(m_mainLayer, 10);

    loadReviews();
    paimon::markDynamicPopup(this);
    return true;
}

void ProfileReviewsPopup::loadReviews() {
    std::string username;
    if (auto gm = GameManager::get()) {
        username = gm->m_playerName;
    }

    std::string endpoint = fmt::format("/api/profile-ratings/{}?username={}", m_accountID, HttpClient::encodeQueryParam(username));

    uint64_t gen = ++m_requestGeneration;
    WeakRef<ProfileReviewsPopup> self = this;
    HttpClient::get().get(endpoint, [self, gen](bool ok, std::string const& resp) {
        auto popup = self.lock();
        if (!popup || gen != popup->m_requestGeneration) return;

        if (popup->m_spinner) {
            popup->m_spinner->dismiss();
            popup->m_spinner = nullptr;
        }

        if (!ok) {
            if (popup->m_averageLabel) popup->m_averageLabel->setString("0.0/5");
            if (popup->m_countLabel) popup->m_countLabel->setString("No ratings yet");
            return;
        }

        auto parsed = matjson::parse(resp);
        if (!parsed.isOk()) return;
        auto root = parsed.unwrap();

        float avg = 0.f;
        int count = 0;
        if (root["average"].isNumber()) avg = static_cast<float>(root["average"].asDouble().unwrapOr(0.0));
        if (root["count"].isNumber()) count = static_cast<int>(root["count"].asInt().unwrapOr(0));

        auto reviews = root["reviews"];

        popup->buildReviewList(avg, count, reviews);
    });
}

void ProfileReviewsPopup::buildReviewList(float average, int count, const matjson::Value& reviews) {
    auto contentSize = m_mainLayer->getContentSize();
    float centerX = contentSize.width / 2.f;

    // Actualiza el header con animacion
    if (m_averageLabel) {
        m_averageLabel->setString(fmt::format("{:.1f}/5", average).c_str());
        m_averageLabel->setScale(0.0f);
        m_averageLabel->runAction(CCEaseBackOut::create(CCScaleTo::create(0.3f, 0.55f)));
    }
    if (m_countLabel) {
        if (count == 0) {
            m_countLabel->setString("No ratings yet");
        } else {
            m_countLabel->setString(fmt::format("{} rating{}", count, count == 1 ? "" : "s").c_str());
        }
    }

    // Area de scroll
    float scrollW = contentSize.width - 30.f;
    float scrollH = contentSize.height - 100.f;
    float scrollX = 15.f;
    float scrollY = 15.f;

    // Crea las celdas
    float cellPadding = 5.f;

    std::vector<CCNode*> cells;

    if (reviews.isArray()) {
        auto arr = reviews.asArray().unwrapOr(std::vector<matjson::Value>{});
        for (auto& item : arr) {
            if (!item.isObject()) continue;
            std::string user = item["username"].asString().unwrapOr("???");
            int stars = item["stars"].asInt().unwrapOr(0);
            std::string msg = item["message"].asString().unwrapOr("");

            auto cell = createReviewCell(user, stars, msg, scrollW);
            if (cell) cells.push_back(cell);
        }
    }

    if (cells.empty()) {
        auto noReviews = CCLabelBMFont::create("No reviews with messages yet", "chatFont.fnt");
        noReviews->setScale(0.6f);
        noReviews->setColor({150, 150, 150});
        noReviews->setPosition({centerX, contentSize.height / 2.f - 20.f});
        // fade in
        noReviews->setOpacity(0);
        noReviews->runAction(CCFadeIn::create(0.5f));
        m_mainLayer->addChild(noReviews);
        return;
    }

    float totalH = 0.f;
    for (auto& c : cells) {
        totalH += c->getContentHeight() + cellPadding;
    }
    totalH = std::max(totalH, scrollH);

    auto container = CCLayer::create();
    container->setContentSize({scrollW, totalH});

    // Apila las celdas de arriba a abajo
    float yPos = totalH;
    for (auto& c : cells) {
        yPos -= c->getContentHeight() + cellPadding;
        c->setPosition({0.f, yPos});
        container->addChild(c);
    }

    auto scroll = geode::ScrollLayer::create({scrollW, scrollH});
    scroll->setPosition({scrollX, scrollY});
    scroll->m_contentLayer->addChild(container);
    scroll->m_contentLayer->setContentSize({scrollW, totalH});
    scroll->scrollToTop();
    m_mainLayer->addChild(scroll);
    m_scrollView = scroll;

    // Anima las celdas
    animateReviewCells(cells);
}

void ProfileReviewsPopup::animateReviewCells(std::vector<CCNode*> const& cells) {
    // Anima cada celda con retardo escalonado
    for (int i = 0; i < (int)cells.size(); i++) {
        auto cell = cells[i];
        auto finalPos = cell->getPosition();

        // Posicion inicial abajo
        cell->setPosition({finalPos.x, finalPos.y - 25.f});

        // Oculta los hijos inicialmente
        if (auto children = cell->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (auto rgba = typeinfo_cast<CCRGBAProtocol*>(child)) {
                    rgba->setOpacity(0);
                }
            }
        }

        float delay = 0.05f * i;  // stagger: 50ms between each cell
        float duration = 0.35f;

        // Desliza hacia arriba
        auto moveAction = CCEaseOut::create(
            CCMoveTo::create(duration, finalPos),
            2.5f
        );

        auto delayedMove = CCSequence::create(
            CCDelayTime::create(delay),
            moveAction,
            nullptr
        );
        cell->runAction(delayedMove);

        // Aparece los hijos con retardo
        if (auto children = cell->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (typeinfo_cast<CCRGBAProtocol*>(child)) {
                    auto fadeIn = CCSequence::create(
                        CCDelayTime::create(delay),
                        CCFadeTo::create(duration, 255),
                        nullptr
                    );
                    child->runAction(fadeIn);
                }
            }
        }
    }
}

CCNode* ProfileReviewsPopup::createReviewCell(std::string const& username, int stars, std::string const& message, float width) {
    float cellH = message.empty() ? 34.f : 52.f;

    auto cell = CCNode::create();
    cell->setContentSize({width, cellH});

    // Fondo oscuro con esquinas redondeadas
    auto bg = paimon::SpriteHelper::createDarkPanel(width, cellH, 50, 5.f);
    bg->setPosition({0, 0});
    cell->addChild(bg, -1);

    // Nombre de usuario
    auto nameLabel = CCLabelBMFont::create(username.c_str(), "goldFont.fnt");
    nameLabel->setScale(0.45f);
    nameLabel->setAnchorPoint({0, 0.5f});
    nameLabel->setPosition({10.f, cellH - 14.f});
    // Limita el ancho
    float maxNameW = width * 0.45f;
    if (nameLabel->getScaledContentWidth() > maxNameW) {
        nameLabel->setScale(maxNameW / nameLabel->getContentWidth());
    }
    cell->addChild(nameLabel);

    // Estrellas a la derecha
    float starStartX = width - 10.f;
    // Dibuja 5 estrellas de derecha a izquierda
    for (int i = 5; i >= 1; i--) {
        auto starSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
        if (!starSpr) continue;
        starSpr->setScale(0.32f);
        starSpr->setColor(i <= stars ? ccc3(255, 255, 50) : ccc3(60, 60, 60));
        starSpr->setAnchorPoint({1, 0.5f});
        starSpr->setPosition({starStartX, cellH - 14.f});
        starStartX -= starSpr->getScaledContentWidth() + 2.f;
        cell->addChild(starSpr);
    }

    // Mensaje
    if (!message.empty()) {
        float maxMsgW = width - 24.f;

        // Intenta renderizar emotes primero
        bool emoteRendered = false;
        if (paimon::emotes::EmoteService::get().isLoaded() &&
            paimon::emotes::EmoteRenderer::hasEmoteSyntax(message))
        {
            auto emoteNode = paimon::emotes::EmoteRenderer::renderComment(
                message, 18.f, maxMsgW, "chatFont.fnt", 0.5f
            );
            if (emoteNode) {
                emoteNode->setAnchorPoint({0.f, 0.5f});
                emoteNode->setPosition({12.f, 12.f});
                cell->addChild(emoteNode);
                emoteRendered = true;
            }
        }

        if (!emoteRendered) {
            auto msgLabel = CCLabelBMFont::create(message.c_str(), "chatFont.fnt");
            msgLabel->setScale(0.5f);
            msgLabel->setAnchorPoint({0, 0.5f});
            msgLabel->setPosition({12.f, 12.f});
            msgLabel->setColor({210, 210, 220});
            if (msgLabel->getScaledContentWidth() > maxMsgW) {
                msgLabel->setScale(maxMsgW / msgLabel->getContentWidth());
            }
            cell->addChild(msgLabel);
        }
    }

    return cell;
}
