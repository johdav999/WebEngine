// Compatibility entry point: browser-only host flow. Use quests-host-smoke.cjs for the full Windows/web matrix.
process.env.QUEST_BROWSER_ONLY='1';
require('./quests-host-smoke.cjs');
