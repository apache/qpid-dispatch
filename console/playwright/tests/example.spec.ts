import { test, expect } from '@playwright/test';

test('example test', async ({ page }) => {
    await page.goto('https://playwright.dev/');
    const title = page.locator('.navbar__inner .navbar__title');
    await expect(title).toHaveText('Playwright');
});

const dispatchHttpPort = process.env.dispatchHttpPort

test('walk-around test', async ({ page }) => {
    await page.goto('/#/login');
    if (dispatchHttpPort) { // will not autoconnect, we must fill connect form and confirm
        // await page.goto('/#/');
        // await page.goto('/#/login');

        // Click #main-content-page-layout-manual-nav >> :nth-match(div:has-text("Port *"), 5)
        await page.click('#main-content-page-layout-manual-nav >> :nth-match(div:has-text("Port *"), 5)');
        // Press a with modifiers
        await page.press('input[name="form-port"]', 'Control+a');
        // Fill input[name="form-port"]
        await page.fill('input[name="form-port"]', dispatchHttpPort);
        // Click [data-testid="connect-button"]
        await Promise.all([
            page.waitForNavigation(/*{ url: '/#/dashboard' }*/),
            page.click('[data-testid="connect-button"]')
        ]);
    } else { // autoconnect
        await page.waitForNavigation({url: '/#/dashboard'})
    }
    // Click text=Dashboard
    await page.click('text=Dashboard');
    // Click text=Routers
    await page.click('text=Routers');
    await expect(page).toHaveURL('/#/overview/routers');
    // Click text=Addresses
    await page.click('text=Addresses');
    await expect(page).toHaveURL('/#/overview/addresses');
    // Click text=Links
    await page.click('text=Links');
    await expect(page).toHaveURL('/#/overview/links');
    // Click text=Connections
    await page.click('text=Connections');
    await expect(page).toHaveURL('/#/overview/connections');
    // Click text=Logs
    await page.click('text=Logs');
    await expect(page).toHaveURL('/#/overview/logs');
    // Click text=Topology
    await page.click('text=Topology');
    await expect(page).toHaveURL('/#/topology');
    // Click text=Message flow
    await page.click('text=Message flow');
    await expect(page).toHaveURL('/#/flow');
    // Click text=Entities
    await page.click('text=Entities');
    await expect(page).toHaveURL('/#/entities');
    // Click text=Schema
    await page.click('text=Schema');
    await expect(page).toHaveURL('/#/schema');
    // Click [aria-label="Toggle Connect Form"]
    await page.click('[aria-label="Toggle Connect Form"]');
    // Click [data-testid="connect-button"]
    await page.click('[data-testid="connect-button"]');
    await expect(page).toHaveURL('/#/login');
    // Click text=Cancel
    await page.click('text=Cancel');
    // Click [aria-label="Toggle Connect Form"]
    await page.click('[aria-label="Toggle Connect Form"]');
});
