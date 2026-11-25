#pragma once

#include "RequestVerificationFlow.hpp"

namespace paimon::twitch {

std::shared_ptr<RequestVerificationFlow> createWebAccountVerification();
std::string webAccountVerificationInfo();

} // namespace paimon::twitch
