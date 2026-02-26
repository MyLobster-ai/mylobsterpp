#pragma once

#include "openclaw/browser/browser_pool.hpp"
#include "openclaw/gateway/protocol.hpp"

namespace openclaw::gateway {

/// Registers browser.open, browser.close, browser.navigate,
/// browser.screenshot, browser.content, browser.click, browser.type,
/// browser.evaluate, browser.wait, browser.scroll, browser.pdf,
/// browser.cookies.get, browser.cookies.set handlers.
void register_browser_handlers(Protocol& protocol,
                               browser::BrowserPool& pool);

} // namespace openclaw::gateway
