#pragma once

#include <string>

namespace paimon::modauth {

void startOrComplete();
void showPanel();
void clearVerifiedSession();
void setVerifiedSession(std::string const& username, int accountID, bool moderator, bool admin);
bool isVerified(bool admin = false);

}
