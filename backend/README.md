# 🔧 Backend — Supabase Self-Hosted

Backend del proyecto **Couples Watch** usando Supabase self-hosted con Docker.

## Requisitos

- **Docker Desktop** (Windows/Mac) o **Docker Engine** (Linux)
- **Docker Compose** v2+
- **Tailscale** instalado en el PC y en ambos móviles (misma tailnet)
- ~2 GB RAM libre para los contenedores

## 🚀 Setup Rápido

### 1. Levantar Supabase

```bash
cd backend
docker-compose up -d
```

Espera ~30 segundos a que PostgreSQL inicie completamente.

### 2. Verificar que todo funciona

```bash
# Ver estado de contenedores
docker-compose ps

# Ver logs de PostgreSQL
docker logs couples-watch-postgres

# Ver logs de Realtime
docker logs couples-watch-realtime
```

### 3. Acceder a Supabase Studio

Abre `http://localhost:3000` en el navegador para ver el panel de administración.

### 4. Verificar tablas

```bash
# Conectar a PostgreSQL
docker exec -it couples-watch-postgres psql -U postgres -d postgres

# Listar tablas
\dt

# Ver contenido inicial
SELECT * FROM locations;
SELECT * FROM watch_config;
```

## 🌐 Configurar Tailscale

### En el PC (servidor)

1. Instala Tailscale: https://tailscale.com/download
2. Inicia sesión con tu cuenta
3. Anota tu IP de Tailscale: `tailscale ip -4` (algo como `100.x.x.x`)

### En los móviles

1. Instala Tailscale desde Play Store
2. Inicia sesión con la **misma cuenta**
3. Verifica conexión: `ping 100.x.x.x` (la IP del PC)

### En la app Flutter

Configura la URL de Supabase en `app/lib/main.dart`:

```dart
const supabaseUrl = 'http://100.x.x.x:8000';  // Tu IP de Tailscale
```

## 📊 Esquema de Base de Datos

| Tabla | Descripción | Realtime |
|---|---|---|
| `locations` | Ubicación GPS de cada usuario (A/B) | ✅ Sí |
| `haptic_events` | Toques hápticos entre usuarios | ✅ Sí |
| `watch_config` | Configuración del reloj por usuario | ❌ No |

### locations

```
user_id (PK) | latitude | longitude | accuracy | polling_mode | updated_at
```

### haptic_events

```
id (UUID PK) | from_user | to_user | created_at | consumed
```

### watch_config

```
user_id (PK) | clock_timeout_s | brightness_percent | colores... | gps_intervals... | updated_at
```

## 🔑 Variables de Entorno

Puedes crear un archivo `.env` en esta carpeta para personalizar:

```env
POSTGRES_PASSWORD=tu-password-seguro
JWT_SECRET=un-token-jwt-de-al-menos-32-caracteres
API_EXTERNAL_URL=http://100.x.x.x:8000
SITE_URL=http://100.x.x.x:3000
```

Si no creas `.env`, se usan los defaults de desarrollo (suficiente para uso personal).

## 🧹 Mantenimiento

### Limpiar eventos hápticos antiguos

```sql
SELECT cleanup_old_haptic_events();
```

### Backup de la base de datos

```bash
docker exec couples-watch-postgres pg_dump -U postgres postgres > backup.sql
```

### Restaurar backup

```bash
docker exec -i couples-watch-postgres psql -U postgres -d postgres < backup.sql
```

### Reiniciar todo

```bash
docker-compose down
docker-compose up -d
```

### Borrar datos y empezar de cero

```bash
docker-compose down -v  # -v borra los volúmenes (datos)
docker-compose up -d
```

## 🐛 Troubleshooting

| Problema | Solución |
|---|---|
| `connection refused` desde móvil | Verifica que Tailscale está activo en ambos dispositivos |
| Realtime no funciona | Revisa logs: `docker logs couples-watch-realtime` |
| PostgreSQL no arranca | Revisa logs: `docker logs couples-watch-postgres` |
| Studio no carga | Espera ~1 min tras `docker-compose up` y refresca |
| Puerto 5432 ocupado | Cambia el puerto en `docker-compose.yml` o para PostgreSQL local |
