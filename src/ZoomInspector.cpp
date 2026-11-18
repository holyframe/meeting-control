#include "ZoomInspector.h"

QString ZoomInspector::buttonInventoryExpression()
{
    return QString::fromUtf8(R"JS(
(() => {
    const nodes = Array.from(
        document.querySelectorAll('button, [role="button"]')
    );

    return nodes.slice(0, 150).map((el, index) => ({
        index,
        tag: el.tagName,
        text: (el.innerText || el.textContent || '')
            .trim()
            .replace(/\s+/g, ' ')
            .slice(0, 120),
        ariaLabel: el.getAttribute('aria-label'),
        title: el.getAttribute('title'),
        ariaPressed: el.getAttribute('aria-pressed'),
        ariaExpanded: el.getAttribute('aria-expanded'),
        disabled: Boolean(el.disabled),
        visible: Boolean(
            el.getClientRects().length &&
            getComputedStyle(el).visibility !== 'hidden'
        )
    }));
})()
)JS");
}
