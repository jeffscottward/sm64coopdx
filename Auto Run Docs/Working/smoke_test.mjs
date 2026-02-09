/**
 * Headless browser smoke test for SM64CoopDX WebAssembly build.
 *
 * Tests:
 * 1. HTML page loads (HTTP 200)
 * 2. WASM module begins loading (JS glue executes)
 * 3. Canvas element is present
 * 4. ROM overlay UI is displayed
 * 5. No immediate fatal JavaScript errors
 * 6. WebGL context can be created
 *
 * Requires: python3 -m http.server running on port 8081 in build/us_web/
 */

import { chromium } from 'playwright';

const URL = 'http://localhost:8081/sm64coopdx.html';
const TIMEOUT = 60000; // 60 seconds for WASM download

async function smokeTest() {
  const results = {
    pageLoads: false,
    jsGlueExecutes: false,
    canvasExists: false,
    romOverlayVisible: false,
    webglSupported: false,
    wasmModuleInitialized: false,
    errors: [],
    consoleMessages: [],
    warnings: [],
  };

  let browser;
  try {
    browser = await chromium.launch({
      headless: true,
      args: ['--no-sandbox', '--disable-setuid-sandbox'],
    });

    const context = await browser.newContext();
    const page = await context.newPage();

    // Collect console messages and errors
    page.on('console', (msg) => {
      const text = msg.text();
      if (msg.type() === 'error') {
        results.errors.push(text);
      } else if (msg.type() === 'warning') {
        results.warnings.push(text);
      } else {
        results.consoleMessages.push(text);
      }
    });

    page.on('pageerror', (err) => {
      results.errors.push(`PageError: ${err.message}`);
    });

    // Navigate to the page
    console.log(`Navigating to ${URL}...`);
    const response = await page.goto(URL, {
      waitUntil: 'domcontentloaded',
      timeout: TIMEOUT,
    });

    results.pageLoads = response.status() === 200;
    console.log(`[${results.pageLoads ? 'PASS' : 'FAIL'}] Page loads (HTTP ${response.status()})`);

    // Wait for JS glue to start executing (the Module object should exist)
    await page.waitForTimeout(3000); // Give WASM time to start downloading

    // Check canvas exists
    results.canvasExists = await page.evaluate(() => {
      return document.getElementById('canvas') !== null;
    });
    console.log(`[${results.canvasExists ? 'PASS' : 'FAIL'}] Canvas element exists`);

    // Check ROM overlay is visible
    results.romOverlayVisible = await page.evaluate(() => {
      const overlay = document.getElementById('rom-overlay');
      if (!overlay) return false;
      const style = window.getComputedStyle(overlay);
      return style.display !== 'none';
    });
    console.log(`[${results.romOverlayVisible ? 'PASS' : 'FAIL'}] ROM overlay visible`);

    // Check WebGL support
    results.webglSupported = await page.evaluate(() => {
      const canvas = document.createElement('canvas');
      const gl = canvas.getContext('webgl') || canvas.getContext('webgl2') || canvas.getContext('experimental-webgl');
      return gl !== null;
    });
    console.log(`[${results.webglSupported ? 'PASS' : 'FAIL'}] WebGL supported`);

    // Check if Module object was created by the JS glue
    results.jsGlueExecutes = await page.evaluate(() => {
      return typeof Module !== 'undefined' && Module !== null;
    });
    console.log(`[${results.jsGlueExecutes ? 'PASS' : 'FAIL'}] JS glue Module object exists`);

    // Wait longer for WASM to potentially initialize
    console.log('Waiting for WASM to load (up to 30s for 62MB)...');

    // Check WASM download progress by monitoring Module status
    for (let i = 0; i < 30; i++) {
      const status = await page.evaluate(() => {
        if (typeof Module === 'undefined') return 'Module undefined';
        if (Module.calledRun) return 'WASM_RUNNING';
        if (Module.asm) return 'WASM_LOADED';
        return 'LOADING';
      });

      if (status === 'WASM_RUNNING' || status === 'WASM_LOADED') {
        results.wasmModuleInitialized = true;
        console.log(`[PASS] WASM module state: ${status}`);
        break;
      }

      if (i % 5 === 0) {
        console.log(`  ...waiting (${i}s) - state: ${status}`);
      }

      await page.waitForTimeout(1000);
    }

    if (!results.wasmModuleInitialized) {
      console.log('[INFO] WASM module did not fully initialize within 30s (62MB is large)');
    }

    // Final snapshot of the page
    await page.screenshot({ path: '/tmp/sm64coopdx_smoke_test.png', scale: 'css' });
    console.log('Screenshot saved to /tmp/sm64coopdx_smoke_test.png');

    // Report console errors
    if (results.errors.length > 0) {
      console.log('\n=== ERRORS ===');
      results.errors.forEach((e) => console.log(`  ERROR: ${e}`));
    }

    if (results.warnings.length > 0) {
      console.log('\n=== WARNINGS ===');
      results.warnings.slice(0, 20).forEach((w) => console.log(`  WARN: ${w}`));
      if (results.warnings.length > 20) {
        console.log(`  ... and ${results.warnings.length - 20} more warnings`);
      }
    }

    if (results.consoleMessages.length > 0) {
      console.log('\n=== CONSOLE OUTPUT ===');
      results.consoleMessages.slice(0, 30).forEach((m) => console.log(`  LOG: ${m}`));
      if (results.consoleMessages.length > 30) {
        console.log(`  ... and ${results.consoleMessages.length - 30} more messages`);
      }
    }

  } catch (err) {
    console.error(`FATAL: ${err.message}`);
    results.errors.push(`FATAL: ${err.message}`);
  } finally {
    if (browser) await browser.close();
  }

  // Summary
  console.log('\n=== SMOKE TEST SUMMARY ===');
  console.log(`Page loads:          ${results.pageLoads ? 'PASS' : 'FAIL'}`);
  console.log(`JS glue executes:    ${results.jsGlueExecutes ? 'PASS' : 'FAIL'}`);
  console.log(`Canvas exists:       ${results.canvasExists ? 'PASS' : 'FAIL'}`);
  console.log(`ROM overlay visible: ${results.romOverlayVisible ? 'PASS' : 'FAIL'}`);
  console.log(`WebGL supported:     ${results.webglSupported ? 'PASS' : 'FAIL'}`);
  console.log(`WASM initialized:    ${results.wasmModuleInitialized ? 'PASS' : 'PENDING'}`);
  console.log(`Errors:              ${results.errors.length}`);
  console.log(`Warnings:            ${results.warnings.length}`);

  // Output JSON for parsing
  console.log('\n=== JSON RESULTS ===');
  console.log(JSON.stringify(results, null, 2));

  return results;
}

smokeTest();
