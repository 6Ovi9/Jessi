-- ============================================================================
-- Couples Watch — Inicialización de Base de Datos
-- ============================================================================
-- Ejecutar tras levantar Docker:
--   docker exec -i couples-watch-postgres psql -U postgres -d postgres < init-db.sql
--
-- O se ejecuta automáticamente si se monta en /docker-entrypoint-initdb.d/
-- ============================================================================

-- ────────────────────────────────────────────────────────────────────────────
-- 1. ROLES Y PERMISOS (necesarios para PostgREST + GoTrue)
-- ────────────────────────────────────────────────────────────────────────────

-- Rol anónimo (sin autenticar)
DO $$
BEGIN
  IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'anon') THEN
    CREATE ROLE anon NOLOGIN NOINHERIT;
  END IF;
END
$$;

-- Rol autenticado (usuario logueado)
DO $$
BEGIN
  IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'authenticated') THEN
    CREATE ROLE authenticated NOLOGIN NOINHERIT;
  END IF;
END
$$;

-- Rol authenticator (PostgREST se conecta con este)
DO $$
BEGIN
  IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'authenticator') THEN
    CREATE ROLE authenticator NOINHERIT LOGIN PASSWORD 'couples-watch-secret-2024';
  END IF;
END
$$;

GRANT anon TO authenticator;
GRANT authenticated TO authenticator;

-- Rol admin para Supabase internals
DO $$
BEGIN
  IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'supabase_admin') THEN
    CREATE ROLE supabase_admin LOGIN PASSWORD 'couples-watch-secret-2024';
  END IF;
END
$$;

DO $$
BEGIN
  IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'supabase_auth_admin') THEN
    CREATE ROLE supabase_auth_admin LOGIN PASSWORD 'couples-watch-secret-2024';
  END IF;
END
$$;

GRANT ALL ON DATABASE postgres TO supabase_admin;
GRANT ALL ON DATABASE postgres TO supabase_auth_admin;

-- GoTrue (auth) necesita SUPERUSER para crear sus tablas de migraciones
ALTER ROLE supabase_auth_admin SUPERUSER;
ALTER ROLE supabase_admin SUPERUSER;
GRANT ALL ON SCHEMA public TO supabase_auth_admin;
GRANT ALL ON SCHEMA public TO supabase_admin;

-- Schemas requeridos por GoTrue y Realtime
CREATE SCHEMA IF NOT EXISTS auth AUTHORIZATION supabase_auth_admin;
GRANT ALL ON SCHEMA auth TO supabase_auth_admin;
GRANT USAGE ON SCHEMA auth TO anon, authenticated;

CREATE SCHEMA IF NOT EXISTS _realtime AUTHORIZATION supabase_admin;
GRANT ALL ON SCHEMA _realtime TO supabase_admin;

-- ────────────────────────────────────────────────────────────────────────────
-- 2. EXTENSIONES
-- ────────────────────────────────────────────────────────────────────────────

CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- ────────────────────────────────────────────────────────────────────────────
-- 3. FUNCIÓN AUXILIAR: auto-update updated_at
-- ────────────────────────────────────────────────────────────────────────────

CREATE OR REPLACE FUNCTION update_updated_at_column()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = NOW();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- ────────────────────────────────────────────────────────────────────────────
-- 4. TABLA: locations — Ubicaciones en tiempo real
-- ────────────────────────────────────────────────────────────────────────────
-- Solo 2 filas: user "A" y user "B"
-- Se hace UPSERT desde la app cada vez que hay nuevo fix GPS

CREATE TABLE IF NOT EXISTS locations (
  user_id       TEXT PRIMARY KEY,               -- "A" o "B"
  latitude      DOUBLE PRECISION NOT NULL,
  longitude     DOUBLE PRECISION NOT NULL,
  accuracy      FLOAT DEFAULT 0,                -- metros
  polling_mode  TEXT DEFAULT 'NEAR',            -- PRECISION/NEAR/FAR/REMOTE (para debug)
  updated_at    TIMESTAMPTZ DEFAULT NOW()
);

CREATE OR REPLACE FUNCTION enforce_location_order()
RETURNS TRIGGER AS $$
BEGIN
  -- Drop the update silently if the incoming timestamp is older or equal
  IF NEW.updated_at <= OLD.updated_at THEN
    RETURN NULL; 
  END IF;
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Eliminar triggers antiguos por si existen de migraciones previas
DROP TRIGGER IF EXISTS trg_locations_updated_at ON locations;
DROP TRIGGER IF EXISTS trg_01_locations_updated_at ON locations;
DROP TRIGGER IF EXISTS trg_locations_order ON locations;

-- Trigger para rechazar actualizaciones out-of-order (Ejecutar SEGUNDO)
-- Nota: trg_locations_updated_at se omite intencionalmente para evitar clock-drift con el cliente
DROP TRIGGER IF EXISTS trg_02_locations_order ON locations;
CREATE TRIGGER trg_02_locations_order
  BEFORE UPDATE ON locations
  FOR EACH ROW
  EXECUTE FUNCTION enforce_location_order();

-- Insertar filas iniciales con fecha muy antigua para no bloquear el primer update (BE Bug 1 Fix)
INSERT INTO locations (user_id, latitude, longitude, updated_at)
VALUES ('A', 0, 0, '1970-01-01 00:00:00Z'), ('B', 0, 0, '1970-01-01 00:00:00Z')
ON CONFLICT (user_id) DO NOTHING;

-- ────────────────────────────────────────────────────────────────────────────
-- 5. TABLA: haptic_events — Eventos hápticos entre usuarios
-- ────────────────────────────────────────────────────────────────────────────
-- Cada toque del reloj genera una fila.
-- La app de la pareja la lee por Realtime, ejecuta la vibración, y marca consumed=true.

CREATE TABLE IF NOT EXISTS haptic_events (
  id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  from_user   TEXT NOT NULL,                    -- "A" o "B"
  to_user     TEXT NOT NULL,                    -- "B" o "A"
  created_at  TIMESTAMPTZ DEFAULT NOW(),
  consumed    BOOLEAN DEFAULT FALSE
);

-- Índice para queries frecuentes (app B busca eventos no consumidos)
CREATE INDEX IF NOT EXISTS idx_haptic_events_to_user_consumed
  ON haptic_events (to_user, consumed)
  WHERE consumed = FALSE;

-- ────────────────────────────────────────────────────────────────────────────
-- 6. TABLA: watch_config — Configuración del reloj (sincronizada desde app)
-- ────────────────────────────────────────────────────────────────────────────
-- Cada usuario tiene su configuración. Se sincroniza app ↔ Supabase ↔ BLE.

CREATE TABLE IF NOT EXISTS watch_config (
  user_id                   TEXT PRIMARY KEY,   -- "A" o "B"

  -- Timers (segundos)
  clock_timeout_s           INT DEFAULT 5,      -- CLOCK → DEEP_SLEEP
  sleep_timeout_s           INT DEFAULT 5,      -- RADAR/DISTANCE → CLOCK

  -- Colores CLOCK_CONNECTED (hex AARRGGBB)
  color_hours_connected     TEXT DEFAULT 'FFFFDCB4',  -- Blanco cálido
  color_minutes_connected   TEXT DEFAULT 'FFFFF5F0',  -- Blanco neutro
  color_seconds_connected   TEXT DEFAULT 'FFC8DCFF',  -- Blanco frío

  -- Colores CLOCK_DISCONNECTED (hex AARRGGBB)
  color_hours_disc          TEXT DEFAULT 'FF001478',  -- Azul oscuro
  color_minutes_disc        TEXT DEFAULT 'FF003CC8',  -- Azul medio
  color_seconds_disc        TEXT DEFAULT 'FF2864FF',  -- Azul brillante

  -- Brillo y batería
  brightness_percent        INT DEFAULT 60,     -- 10-100%
  low_battery_threshold     INT DEFAULT 15,     -- % para LOW_BATTERY
  logarithmic_brightness    BOOLEAN DEFAULT TRUE, -- Curva gamma

  -- Vibración y Hápticos Phase 2
  haptic_pattern            TEXT DEFAULT 'both',
  color_haptic_tx           TEXT DEFAULT 'FF66CCFF',  -- Azul claro (Toque enviado)
  color_haptic_rx           TEXT DEFAULT 'FFFF6699',  -- Rosa (Toque recibido)
  brightness_haptic_tx      INT DEFAULT 100,
  brightness_haptic_rx      INT DEFAULT 100,

  -- Gyroscope wrist-flick timing window (ms)
  double_flick_window_ms    INT DEFAULT 800,

  -- GPS Dynamic Polling (segundos)
  gps_interval_precision_s  INT DEFAULT 3,      -- <500m o RADAR activo
  gps_interval_near_s       INT DEFAULT 60,     -- <10km
  gps_interval_far_s        INT DEFAULT 180,    -- 10-50km
  gps_interval_remote_min_s INT DEFAULT 300,    -- >50km (min)
  gps_interval_remote_max_s INT DEFAULT 600,    -- >50km (max, con jitter)

  -- IMU wake-on-motion threshold
  wake_threshold            INT DEFAULT 2,      -- 0x00-0xFF (default 0x02 = ~312mg)

  -- Gyroscope wrist-flick threshold (dps)
  gyro_threshold            INT DEFAULT 260,

  -- Metadata
  updated_at                TIMESTAMPTZ DEFAULT NOW()
);

-- Trigger para auto-actualizar updated_at
DROP TRIGGER IF EXISTS trg_watch_config_updated_at ON watch_config;
CREATE TRIGGER trg_watch_config_updated_at
  BEFORE UPDATE ON watch_config
  FOR EACH ROW
  EXECUTE FUNCTION update_updated_at_column();

-- Insertar configs iniciales
INSERT INTO watch_config (user_id)
VALUES ('A'), ('B')
ON CONFLICT (user_id) DO NOTHING;

-- Compatibilidad con instalaciones existentes: asegurar columnas nuevas
ALTER TABLE watch_config
  ADD COLUMN IF NOT EXISTS haptic_pattern TEXT DEFAULT 'both';

ALTER TABLE watch_config
  ADD COLUMN IF NOT EXISTS color_haptic_tx TEXT DEFAULT 'FF66CCFF';

ALTER TABLE watch_config
  ADD COLUMN IF NOT EXISTS color_haptic_rx TEXT DEFAULT 'FFFF6699';

ALTER TABLE watch_config
  ADD COLUMN IF NOT EXISTS brightness_haptic_tx INT DEFAULT 100;

ALTER TABLE watch_config
  ADD COLUMN IF NOT EXISTS brightness_haptic_rx INT DEFAULT 100;

ALTER TABLE watch_config
  ADD COLUMN IF NOT EXISTS double_flick_window_ms INT DEFAULT 800;

ALTER TABLE watch_config
  ALTER COLUMN haptic_pattern SET DEFAULT 'both';

-- ────────────────────────────────────────────────────────────────────────────
-- 7. ROW LEVEL SECURITY (RLS)
-- ────────────────────────────────────────────────────────────────────────────
-- Política simple: usuarios autenticados pueden leer y escribir todo.
-- En un sistema de 2 usuarios esto es suficiente.
-- Para producción con más usuarios, añadir filtro por user_id.

ALTER TABLE locations ENABLE ROW LEVEL SECURITY;
ALTER TABLE haptic_events ENABLE ROW LEVEL SECURITY;
ALTER TABLE watch_config ENABLE ROW LEVEL SECURITY;

-- Políticas para usuarios autenticados
CREATE POLICY "Authenticated users can read all locations"
  ON locations FOR SELECT TO authenticated USING (true);

CREATE POLICY "Authenticated users can upsert any location"
  ON locations FOR ALL TO authenticated USING (true) WITH CHECK (true);

CREATE POLICY "Authenticated users can read haptic events"
  ON haptic_events FOR SELECT TO authenticated USING (true);

CREATE POLICY "Authenticated users can insert haptic events"
  ON haptic_events FOR INSERT TO authenticated WITH CHECK (true);

CREATE POLICY "Authenticated users can update haptic events"
  ON haptic_events FOR UPDATE TO authenticated USING (true) WITH CHECK (true);

CREATE POLICY "Authenticated users can read watch config"
  ON watch_config FOR SELECT TO authenticated USING (true);

CREATE POLICY "Authenticated users can update any config"
  ON watch_config FOR ALL TO authenticated USING (true) WITH CHECK (true);

-- Permisos de tabla para los roles
GRANT USAGE ON SCHEMA public TO anon, authenticated;
GRANT ALL ON ALL TABLES IN SCHEMA public TO anon, authenticated;
GRANT ALL ON ALL SEQUENCES IN SCHEMA public TO anon, authenticated;

-- ────────────────────────────────────────────────────────────────────────────
-- 8. REALTIME — Habilitar tablas para subscripción en tiempo real
-- ────────────────────────────────────────────────────────────────────────────
-- Supabase Realtime escucha cambios en estas tablas via Postgres logical replication.

-- Nota: En Supabase self-hosted, la habilitación de Realtime se hace via
-- la publicación 'supabase_realtime'. Si no existe, la creamos.

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_publication WHERE pubname = 'supabase_realtime'
  ) THEN
    CREATE PUBLICATION supabase_realtime;
  END IF;
END
$$;

-- Añadir tablas a la publicación de Realtime de forma idempotente
DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_publication_tables 
    WHERE pubname = 'supabase_realtime' AND schemaname = 'public' AND tablename = 'locations'
  ) THEN
    ALTER PUBLICATION supabase_realtime ADD TABLE locations;
  END IF;

  IF NOT EXISTS (
    SELECT 1 FROM pg_publication_tables 
    WHERE pubname = 'supabase_realtime' AND schemaname = 'public' AND tablename = 'haptic_events'
  ) THEN
    ALTER PUBLICATION supabase_realtime ADD TABLE haptic_events;
  END IF;
END
$$;
-- watch_config no necesita Realtime (se sincroniza por BLE, no por WebSocket)

-- ────────────────────────────────────────────────────────────────────────────
-- 9. FUNCIÓN: Limpiar eventos hápticos antiguos (mantenimiento)
-- ────────────────────────────────────────────────────────────────────────────

CREATE OR REPLACE FUNCTION cleanup_old_haptic_events()
RETURNS void AS $$
BEGIN
  DELETE FROM haptic_events
  WHERE consumed = TRUE
    AND created_at < NOW() - INTERVAL '24 hours';
END;
$$ LANGUAGE plpgsql SECURITY DEFINER SET search_path = public;

-- Configurar pg_cron para que ejecute el mantenimiento periódicamente (BE Bug 10 Fix)
CREATE EXTENSION IF NOT EXISTS pg_cron;

DO $$
BEGIN
  -- Programar la limpieza cada hora si no está programada ya
  IF NOT EXISTS (SELECT 1 FROM cron.job WHERE jobname = 'cleanup_haptic_events') THEN
    PERFORM cron.schedule('cleanup_haptic_events', '0 * * * *', 'SELECT public.cleanup_old_haptic_events();');
  END IF;
END
$$;

-- ────────────────────────────────────────────────────────────────────────────
-- DONE
-- ────────────────────────────────────────────────────────────────────────────

-- Verificación:
-- SELECT table_name FROM information_schema.tables WHERE table_schema = 'public';
-- Esperado: locations, haptic_events, watch_config
