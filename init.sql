-- init.sql
-- SQLite initialization script for FaaS platform

-- Routes table: key = "METHOD:URI", value = complete function config (JSON)
CREATE TABLE IF NOT EXISTS functions (
  k TEXT PRIMARY KEY,                   -- Key: "METHOD:URI" (e.g., "POST:/resize")
  v TEXT NOT NULL,                      -- Value: Complete JSON config
  updated INTEGER DEFAULT (strftime('%s','now'))  -- UNIX timestamp
);

CREATE INDEX IF NOT EXISTS idx_functions_updated ON functions(updated);

-- ===================================================================
-- Routes with complete function configuration
-- ===================================================================

INSERT OR REPLACE INTO functions (k, v, updated)
VALUES (
  'POST:/resize',
  '{"name":"resizeImage","runtime":"wasm","module":"/opt/functions/resizeImage/module.wasm","handler":"resize_handler","memory":256,"timeout":10}',
  strftime('%s','now')
);

INSERT OR REPLACE INTO functions (k, v, updated)
VALUES (
  'GET:/ping',
  '{"name":"ping","runtime":"wasm","module":"/opt/functions/ping/module.wasm","handler":"ping","memory":128,"timeout":5}',
  strftime('%s','now')
);

-- PHP function examples
INSERT OR REPLACE INTO functions (k, v, updated)
VALUES (
  'POST:/api/hello',
  '{"name":"helloPhp","runtime":"php","module":"/opt/functions/hello/hello.php","handler":"main","memory":64,"timeout":5}',
  strftime('%s','now')
);

INSERT OR REPLACE INTO functions (k, v, updated)
VALUES (
  'GET:/api/info',
  '{"name":"phpInfo","runtime":"php","module":"/opt/functions/info/info.php","handler":"main","memory":64,"timeout":5}',
  strftime('%s','now')
);

-- Python WASM function
INSERT OR REPLACE INTO functions (k, v, updated)
VALUES (
  'GET:/python',
  '{"name":"app","runtime":"wasm","module":"/opt/functions/app/app.wasm","handler":"main","memory":128,"timeout":10}',
  strftime('%s','now')
);
