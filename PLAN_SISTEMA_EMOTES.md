# Plan Tecnico: Sistema de Emotes Paimbnails

## 1. Resumen Ejecutivo

Este plan define la arquitectura completa para el sistema de emotes de Paimbnails, que consta de:
- **Bot de Discord** (Render): Maneja interacciones, moderacion y submissions
- **Servidor de Emotes** (Vercel): Almacena, sirve y gestiona emotes con carga por chunks

## 2. Arquitectura General

```
+-----------------------------------+         +-----------------------------------+
|         Bot Discord (Render)      |         |    Servidor Emotes (Vercel)       |
|                                   |         |                                   |
|  +---------------------------+    |         |  +---------------------------+    |
|  | cogs/emote_submissions.py |    |         |  |  API Routes (Next.js)     |    |
|  | - Modal de submission     |    |         |  |  - /api/emotes/upload     |    |
|  | - Botones aprobar/rechazar|    |         |  |  - /api/emotes/list       |    |
|  | - Estado pendiente        |    |         |  |  - /api/emotes/chunk      |    |
|  +---------------------------+    |         |  |  - /api/emotes/pending    |    |
|  +---------------------------+    |         |  |  - /api/emotes/approve    |    |
|  |   cogs/emotes.py          |    |         |  |  - /api/emotes/reject     |    |
|  |   - /addemotes (admin)    |    |         |  |  - /api/emotes/serve/:id  |    |
|  |   - /crearcategoria       |    |         |  +---------------------------+    |
|  |   - Categorias            |    |         |  +---------------------------+    |
|  +---------------------------+    |         |  |  Storage (Vercel Blob/   |    |
|  +---------------------------+    |         |  |  Bunny CDN / Cloudflare   |    |
|  |  utils/api_client.py      |    |         |  |  R2)                      |    |
|  |  - upload_emote()         |<---|----REST-->|  - Emotes aprobados       |    |
|  |  - get_emotes() [NUEVO]   |<---|----REST-->|  - Emotes pendientes      |    |
|  |  - approve_emote() [NUEVO]|<---|----REST-->|  - Metadata JSON          |    |
|  +---------------------------+    |         |  +---------------------------+    |
+-----------------------------------+         +-----------------------------------+
```

## 3. Estado Actual del Sistema

### 3.1 Bot de Discord (Render) - EXISTENTE

El bot ya cuenta con:

**cogs/emote_submissions.py:**
- `EmoteSubmissionModal`: Modal con nombre + categoria
- `SubmitEmoteButton`: Boton persistente para abrir modal
- `ModerationView`: Botones de aprobar/rechazar
- Estado `PENDING` en memoria (dict `_pending`)
- Envio a canal de moderacion configurado
- Notificacion DM al usuario

**cogs/emotes.py:**
- `/crearcategoria`: Crea categorias (admin)
- `/addemotes`: Sube emote directo (admin)
- `/ytlink`: Guarda links de YouTube
- Autocomplete de categorias
- Broadcast de notificaciones

**utils/api_client.py:**
- `upload_emote()`: POST multipart a `/api/paimon-emote/upload`
- `save_ytlink()`: POST a `/api/ytlinks`
- `get_bot_config()`: GET/POST a `/api/bot-config`

### 3.2 Servidor Vercel - INCOMPLETO

Actualmente solo tiene `package.json` con Next.js 14, sin implementacion de API routes.

## 4. Nuevos Requerimientos

### 4.1 Sistema de Sugerencias (Formulario)
- [x] Formulario con nombre y categoria (YA EXISTE)
- [x] Estado pendiente (YA EXISTE en memoria)
- [ ] Persistencia de pendientes en servidor (NUEVO)
- [x] Botones aprobar/rechazar (YA EXISTE)
- [ ] Carga por chunks para listar emotes (NUEVO)

### 4.2 Carga por Chunks
- Endpoint paginado: `GET /api/emotes?page=1&limit=50`
- Cursor-based o offset-based pagination
- Categorias filtrables: `GET /api/emotes?category=reaction`
- Busqueda: `GET /api/emotes?search=paimon`

### 4.3 Servidor Vercel (API Completa)
Necesita implementar:
- Subida de emotes (aprobados)
- Cola de emotes pendientes
- Aprobacion/rechazo de emotes
- Listado paginado (chunks)
- Servir emotes estaticos
- Storage de metadata

## 5. Implementacion del Servidor Vercel

### 5.1 Estructura de Archivos

```
servers/PaimbnailsEmote-vercelserver/
├── app/
│   ├── api/
│   │   ├── emotes/
│   │   │   ├── upload/route.ts       # POST: Subir emote directo
│   │   │   ├── list/route.ts         # GET: Listar emotes (chunks)
│   │   │   ├── pending/route.ts      # GET/POST: Cola de pendientes
│   │   │   ├── approve/route.ts      # POST: Aprobar emote
│   │   │   ├── reject/route.ts       # POST: Rechazar emote
│   │   │   └── [id]/route.ts         # GET: Obtener emote especifico
│   │   ├── bot-config/route.ts       # GET/POST: Config del bot
│   │   └── paimon-emote/
│   │       └── upload/route.ts       # Legacy support
│   ├── page.tsx                      # Landing page
│   └── layout.tsx
├── lib/
│   ├── storage.ts                    # Abstraccion de storage
│   ├── db.ts                         # Interfaz de base de datos
│   └── auth.ts                       # Middleware de autenticacion
├── types/
│   └── emote.ts                      # Tipos TypeScript
├── public/
│   └── emotes/                       # Emotes servidos estaticamente
├── package.json
├── next.config.js
└── vercel.json
```

### 5.2 Modelo de Datos

```typescript
// types/emote.ts

interface Emote {
  id: string;              // UUID o hash
  name: string;            // Nombre del emote
  category: string;        // Categoria
  status: "pending" | "approved" | "rejected";
  url: string;             // URL publica del emote
  fileData?: string;       // Base64 o referencia a storage
  filename: string;
  format: string;          // png, gif, jpg, webp, apng
  size: number;            // Bytes
  uploadedBy: string;      // Discord username
  uploadedById: string;    // Discord user ID
  approvedBy?: string;     // Discord username del mod
  approvedAt?: string;     // ISO timestamp
  createdAt: string;       // ISO timestamp
  updatedAt: string;       // ISO timestamp
}

interface EmoteListResponse {
  emotes: Emote[];
  total: number;
  page: number;
  limit: number;
  hasMore: boolean;
  categories: string[];    // Categorias disponibles
}

interface PendingEmote extends Emote {
  status: "pending";
  moderatorMessageId?: string;  // ID del mensaje en Discord
}
```

### 5.3 API Endpoints

#### POST /api/emotes/upload
Sube un emote directamente (para admins).

**Request:**
```
Content-Type: multipart/form-data
X-API-Key: <api_key>

file: <binary>
category: "reaction"
format: "png"
```

**Response:**
```json
{
  "success": true,
  "emote": {
    "id": "abc123",
    "name": "paimon_happy",
    "category": "reaction",
    "url": "https://cdn.example.com/emotes/abc123.png",
    "type": "png",
    "size": 24580
  }
}
```

#### GET /api/emotes/list
Lista emotes aprobados con paginacion (chunks).

**Query Params:**
- `page` (number, default: 1)
- `limit` (number, default: 50, max: 100)
- `category` (string, optional)
- `search` (string, optional)
- `sort` (string: "newest" | "oldest" | "name", default: "newest")

**Response:**
```json
{
  "emotes": [
    {
      "id": "abc123",
      "name": "paimon_happy",
      "category": "reaction",
      "url": "https://cdn.example.com/emotes/abc123.png",
      "format": "png",
      "size": 24580,
      "uploadedBy": "usuario123",
      "createdAt": "2024-01-15T10:30:00Z"
    }
  ],
  "total": 150,
  "page": 1,
  "limit": 50,
  "hasMore": true,
  "categories": ["reaction", "meme", "sticker"]
}
```

#### POST /api/emotes/pending
Guarda un emote en estado pendiente.

**Request:**
```json
{
  "name": "paimon_happy",
  "category": "reaction",
  "fileData": "base64...",
  "filename": "happy.png",
  "format": "png",
  "size": 24580,
  "uploadedBy": "usuario123",
  "uploadedById": "123456789"
}
```

**Response:**
```json
{
  "success": true,
  "pendingId": "pend_abc123",
  "status": "pending"
}
```

#### GET /api/emotes/pending
Lista emotes pendientes (para moderacion).

**Headers:**
- `X-API-Key: <api_key>`

**Query Params:**
- `page` (number, default: 1)
- `limit` (number, default: 20)

**Response:**
```json
{
  "pending": [
    {
      "id": "pend_abc123",
      "name": "paimon_happy",
      "category": "reaction",
      "status": "pending",
      "uploadedBy": "usuario123",
      "createdAt": "2024-01-15T10:30:00Z"
    }
  ],
  "total": 5,
  "page": 1,
  "limit": 20,
  "hasMore": false
}
```

#### POST /api/emotes/approve
Aprueba un emote pendiente.

**Request:**
```json
{
  "pendingId": "pend_abc123",
  "approvedBy": "moderador123",
  "approvedById": "987654321"
}
```

**Headers:**
- `X-API-Key: <api_key>`

**Response:**
```json
{
  "success": true,
  "emote": {
    "id": "abc123",
    "name": "paimon_happy",
    "category": "reaction",
    "url": "https://cdn.example.com/emotes/abc123.png",
    "status": "approved"
  }
}
```

#### POST /api/emotes/reject
Rechaza un emote pendiente.

**Request:**
```json
{
  "pendingId": "pend_abc123",
  "rejectedBy": "moderador123",
  "reason": "Contenido inapropiado"
}
```

**Headers:**
- `X-API-Key: <api_key>`

**Response:**
```json
{
  "success": true,
  "pendingId": "pend_abc123",
  "status": "rejected"
}
```

#### GET /api/emotes/[id]
Obtiene un emote especifico.

**Response:**
```json
{
  "id": "abc123",
  "name": "paimon_happy",
  "category": "reaction",
  "url": "https://cdn.example.com/emotes/abc123.png",
  "format": "png",
  "size": 24580,
  "status": "approved",
  "uploadedBy": "usuario123",
  "createdAt": "2024-01-15T10:30:00Z"
}
```

### 5.4 Storage Options

Opcion A: **Vercel Blob Storage**
- Pros: Nativo de Vercel, CDN integrado, facil de usar
- Cons: Limitado en plan gratuito (250 MB)
- Uso: Almacenar emotes directamente

Opcion B: **Cloudflare R2**
- Pros: Compatible S3, sin egress fees, 10GB gratis
- Cons: Requiere configuracion extra
- Uso: Almacenar emotes y metadata

Opcion C: **Bunny CDN Storage**
- Pros: Barato, CDN global, ya usado en el proyecto
- Cons: Requiere cuenta y API key
- Uso: Almacenar emotes aprobados

**Recomendacion:**
- **Emotes aprobados:** Bunny CDN (ya usado en el proyecto para thumbnails)
- **Emotes pendientes:** Vercel Blob o base de datos (pequenos, temporales)
- **Metadata:** Vercel Postgres (Neon) o JSON en Blob/R2

### 5.5 Autenticacion

Middleware para verificar `X-API-Key`:

```typescript
// lib/auth.ts
import { NextRequest, NextResponse } from "next/server";

export function verifyApiKey(req: NextRequest): boolean {
  const apiKey = req.headers.get("X-API-Key");
  const validKey = process.env.PAIMON_API_KEY;
  return apiKey === validKey;
}

export function requireAuth(handler: Function) {
  return async (req: NextRequest, ...args: any[]) => {
    if (!verifyApiKey(req)) {
      return NextResponse.json(
        { error: "Unauthorized" },
        { status: 401 }
      );
    }
    return handler(req, ...args);
  };
}
```

## 6. Mejoras en el Bot de Discord

### 6.1 Persistencia de Emotes Pendientes

Actualmente los pendientes se guardan en memoria (`self._pending: dict[int, dict]`).

**Problema:** Se pierden al reiniciar el bot.

**Solucion:** Guardar en el servidor Vercel via API.

Cambios en `cogs/emote_submissions.py`:

```python
# Nuevo metodo en api_client.py
async def create_pending_emote(self, name: str, category: str, 
                                file_data: bytes, filename: str,
                                uploaded_by: str, uploaded_by_id: str) -> dict | None:
    """Guarda un emote en estado pendiente en el servidor."""
    url = f"{self.emote_base_url}/api/emotes/pending"
    import base64
    body = {
        "name": name,
        "category": category,
        "fileData": base64.b64encode(file_data).decode(),
        "filename": filename,
        "format": filename.rsplit(".", 1)[-1] if "." in filename else "png",
        "size": len(file_data),
        "uploadedBy": uploaded_by,
        "uploadedById": str(uploaded_by_id),
    }
    try:
        async with self.session.post(
            url, json=body, headers=self._auth_headers()
        ) as resp:
            if resp.status in (200, 201):
                return await resp.json()
    except Exception as e:
        logger.error("Error creating pending emote: %s", e)
    return None

async def approve_pending_emote(self, pending_id: str, 
                                 approved_by: str, approved_by_id: str) -> dict | None:
    """Aprueba un emote pendiente."""
    url = f"{self.emote_base_url}/api/emotes/approve"
    body = {
        "pendingId": pending_id,
        "approvedBy": approved_by,
        "approvedById": str(approved_by_id),
    }
    return await self._post(url, body, base_url=self.emote_base_url)

async def reject_pending_emote(self, pending_id: str,
                                rejected_by: str, reason: str = "") -> dict | None:
    """Rechaza un emote pendiente."""
    url = f"{self.emote_base_url}/api/emotes/reject"
    body = {
        "pendingId": pending_id,
        "rejectedBy": rejected_by,
        "reason": reason,
    }
    return await self._post(url, body, base_url=self.emote_base_url)
```

### 6.2 Carga por Chunks

Nuevo metodo en `api_client.py`:

```python
async def get_emotes(
    self, 
    page: int = 1, 
    limit: int = 50, 
    category: str = "",
    search: str = ""
) -> dict | None:
    """Obtiene emotes paginados (chunks) del servidor."""
    params = f"?page={page}&limit={limit}"
    if category:
        params += f"&category={category}"
    if search:
        params += f"&search={search}"
    
    url = f"{self.emote_base_url}/api/emotes/list{params}"
    try:
        async with self.session.get(url) as resp:
            if resp.status == 200:
                return await resp.json()
    except Exception as e:
        logger.error("Error fetching emotes: %s", e)
    return None
```

Nuevo comando en `cogs/emotes.py`:

```python
@app_commands.command(
    name="emotes",
    description="Lista los emotes disponibles (carga por chunks)",
)
@app_commands.describe(
    categoria="Filtrar por categoria",
    busqueda="Buscar por nombre",
)
@app_commands.checks.cooldown(1, 5.0)
async def list_emotes(
    self,
    interaction: discord.Interaction,
    categoria: str = "",
    busqueda: str = "",
):
    await interaction.response.defer(ephemeral=True)
    
    result = await self.api.get_emotes(
        page=1, 
        limit=50, 
        category=categoria,
        search=busqueda
    )
    
    if not result or not result.get("emotes"):
        await interaction.followup.send(
            embed=make_embed("😕 Sin Resultados", "No se encontraron emotes.", COLOR_INFO)
        )
        return
    
    emotes = result["emotes"]
    total = result.get("total", 0)
    has_more = result.get("hasMore", False)
    
    # Crear embed paginado
    embed = make_embed(
        title=f"🎨 Emotes Disponibles ({total})",
        description=f"Pagina 1 • {len(emotes)} emotes mostrados",
        color=COLOR_INFO,
    )
    
    for emote in emotes[:25]:  # Max 25 fields
        embed.add_field(
            name=f":{emote['name']}:",
            value=f"Categoria: `{emote['category']}` | [Ver]({emote['url']})",
            inline=True,
        )
    
    if has_more:
        embed.set_footer(text="Usa /emotes de nuevo o navega con los botones (proximamente)")
    
    await interaction.followup.send(embed=embed)
```

### 6.3 Paginacion con Botones

Crear vista de paginacion:

```python
class EmotePaginationView(discord.ui.View):
    """Navegacion por chunks de emotes."""
    
    def __init__(self, api: APIClient, category: str = "", search: str = ""):
        super().__init__(timeout=180)
        self.api = api
        self.category = category
        self.search = search
        self.page = 1
        self.limit = 50
        self.emotes = []
        self.has_more = False
    
    async def load_page(self):
        result = await self.api.get_emotes(
            page=self.page,
            limit=self.limit,
            category=self.category,
            search=self.search,
        )
        if result:
            self.emotes = result.get("emotes", [])
            self.has_more = result.get("hasMore", False)
    
    def build_embed(self) -> discord.Embed:
        embed = make_embed(
            title=f"🎨 Emotes (Pagina {self.page})",
            description=f"{len(self.emotes)} emotes",
            color=COLOR_INFO,
        )
        for emote in self.emotes[:25]:
            embed.add_field(
                name=f":{emote['name']}:",
                value=f"`{emote['category']}`",
                inline=True,
            )
        return embed
    
    @discord.ui.button(label="◀ Anterior", style=discord.ButtonStyle.secondary)
    async def prev_button(self, interaction: discord.Interaction, button: discord.ui.Button):
        if self.page > 1:
            self.page -= 1
            await self.load_page()
            await interaction.response.edit_message(embed=self.build_embed(), view=self)
    
    @discord.ui.button(label="Siguiente ▶", style=discord.ButtonStyle.secondary)
    async def next_button(self, interaction: discord.Interaction, button: discord.ui.Button):
        if self.has_more:
            self.page += 1
            await self.load_page()
            await interaction.response.edit_message(embed=self.build_embed(), view=self)
```

## 7. Flujo Completo de Datos

### 7.1 Submision de Emote (Usuario)

```
1. Usuario hace clic en "📤 Enviar Emote"
2. Bot abre EmoteSubmissionModal
3. Usuario ingresa nombre + categoria
4. Bot valida nombre y categoria
5. Bot pide imagen (mensaje en canal)
6. Usuario sube imagen
7. Bot valida formato y tamano
8. Bot guarda en Vercel via POST /api/emotes/pending
9. Bot crea mensaje en canal de moderacion
10. Bot notifica al usuario: "Emote enviado a revision"
```

### 7.2 Aprobacion de Emote (Moderador)

```
1. Moderador ve emote en canal de moderacion
2. Moderador hace clic en "✅ Aprobar"
3. Bot verifica permisos (manage_messages)
4. Bot envia POST /api/emotes/approve con pendingId
5. Servidor Vercel:
   a. Mueve archivo de pendientes a aprobados
   b. Genera URL publica
   c. Actualiza metadata
   d. Devuelve datos del emote aprobado
6. Bot actualiza mensaje de moderacion (verde)
7. Bot notifica al usuario via DM
8. Bot ejecuta broadcast a canales de notificacion
```

### 7.3 Rechazo de Emote (Moderador)

```
1. Moderador hace clic en "❌ Rechazar"
2. Bot verifica permisos
3. Bot envia POST /api/emotes/reject con pendingId
4. Servidor Vercel marca como rechazado (o elimina)
5. Bot actualiza mensaje de moderacion (rojo)
6. Bot notifica al usuario via DM
```

### 7.4 Carga de Emotes (Listado)

```
1. Usuario ejecuta /emotes [categoria] [busqueda]
2. Bot envia GET /api/emotes/list?page=1&limit=50
3. Servidor Vercel:
   a. Lee metadata de emotes aprobados
   b. Aplica filtros (categoria, busqueda)
   c. Pagina resultados
   d. Devuelve chunk de 50 emotes
4. Bot crea embed con emotes
5. Si hay mas paginas, muestra botones de navegacion
6. Usuario navega con botones (carga siguiente chunk)
```

## 8. Plan de Implementacion

### Fase 1: Servidor Vercel (Semana 1)
- [ ] Configurar proyecto Next.js 14+ con App Router
- [ ] Implementar API routes basicos
- [ ] Configurar storage (Bunny CDN / Vercel Blob)
- [ ] Implementar autenticacion (X-API-Key)
- [ ] Crear endpoints: upload, list, pending, approve, reject
- [ ] Testing local con curl/Postman
- [ ] Deploy a Vercel

### Fase 2: Integracion Bot (Semana 2)
- [ ] Actualizar api_client.py con nuevos metodos
- [ ] Modificar emote_submissions.py para usar servidor
- [ ] Implementar persistencia de pendientes
- [ ] Crear comando /emotes con paginacion
- [ ] Implementar EmotePaginationView
- [ ] Testing en servidor de prueba

### Fase 3: Optimizacion (Semana 3)
- [ ] Implementar caching de chunks en bot
- [ ] Optimizar queries del servidor
- [ ] Agregar metricas y logs
- [ ] Implementar rate limiting
- [ ] Testing de carga (stress test)

### Fase 4: Migracion y Deploy (Semana 4)
- [ ] Backup de emotes existentes
- [ ] Migrar emotes actuales al nuevo servidor
- [ ] Actualizar variables de entorno en Render
- [ ] Deploy bot a produccion
- [ ] Monitoreo post-deploy

## 9. Variables de Entorno

### Bot de Discord (Render)
```
DISCORD_TOKEN=<token>
API_BASE_URL=https://api.flozwer.org
EMOTE_BASE_URL=https://paimbnails-emote.vercel.app
PAIMON_API_KEY=<api_key>
PAIMON_MOD_CODE=<mod_code>
ADMIN_DISCORD_ID=<discord_id>
ALLOWED_GUILD_ID=<guild_id>
```

### Servidor Vercel
```
PAIMON_API_KEY=<same_as_bot>
BUNNY_STORAGE_API_KEY=<bunny_key>
BUNNY_STORAGE_ZONE=<zone>
BUNNY_CDN_URL=https://<zone>.b-cdn.net
VERCEL_BLOB_TOKEN=<blob_token>  # Opcional
DATABASE_URL=<postgres_url>      # Opcional
```

## 10. Consideraciones Tecnicas

### 10.1 Rate Limiting
- Bot: Cooldowns por comando (ya implementado)
- Servidor: Limitar a 100 req/min por IP
- API Key: Limitar a 1000 req/min por key

### 10.2 Seguridad
- Validar extensiones de archivo (whitelist)
- Limitar tamano (5 MB max)
- Sanitizar nombres (alphanumeric + underscore)
- Verificar API Key en endpoints sensibles
- No exponer fileData en respuestas publicas

### 10.3 Performance
- CDN para emotes aprobados (cache 1 ano)
- Paginacion max 100 items por request
- Compresion de imagenes al subir (opcional)
- Lazy loading en lista de emotes

### 10.4 Escalabilidad
- Si hay +1000 emotes: usar base de datos (PostgreSQL)
- Si archivos son grandes: usar storage dedicado (S3/R2)
- Cache en Redis para listados frecuentes

## 11. Codigo de Referencia

### 11.1 Server: app/api/emotes/list/route.ts

```typescript
import { NextRequest, NextResponse } from "next/server";
import { getApprovedEmotes, getCategories } from "@/lib/db";

export async function GET(request: NextRequest) {
  const { searchParams } = new URL(request.url);
  
  const page = Math.max(1, parseInt(searchParams.get("page") || "1"));
  const limit = Math.min(100, Math.max(1, parseInt(searchParams.get("limit") || "50")));
  const category = searchParams.get("category") || "";
  const search = searchParams.get("search") || "";
  const sort = searchParams.get("sort") || "newest";
  
  try {
    const result = await getApprovedEmotes({
      page,
      limit,
      category,
      search,
      sort,
    });
    
    const categories = await getCategories();
    
    return NextResponse.json({
      ...result,
      categories,
    });
  } catch (error) {
    console.error("Error listing emotes:", error);
    return NextResponse.json(
      { error: "Failed to list emotes" },
      { status: 500 }
    );
  }
}
```

### 11.2 Server: lib/db.ts (con JSON en Blob)

```typescript
import { put, get, list } from "@vercel/blob";

const EMOTES_PREFIX = "emotes/";
const PENDING_PREFIX = "pending/";
const METADATA_KEY = "emotes/metadata.json";

interface EmoteMetadata {
  emotes: Emote[];
  pending: Emote[];
  categories: string[];
  lastUpdated: string;
}

async function getMetadata(): Promise<EmoteMetadata> {
  try {
    const blob = await get(METADATA_KEY);
    if (blob) {
      const text = await blob.text();
      return JSON.parse(text);
    }
  } catch (e) {
    console.error("Error reading metadata:", e);
  }
  return { emotes: [], pending: [], categories: [], lastUpdated: new Date().toISOString() };
}

async function saveMetadata(metadata: EmoteMetadata) {
  metadata.lastUpdated = new Date().toISOString();
  await put(METADATA_KEY, JSON.stringify(metadata), {
    contentType: "application/json",
    access: "public",
  });
}

export async function getApprovedEmotes(filters: {
  page: number;
  limit: number;
  category?: string;
  search?: string;
  sort?: string;
}) {
  const metadata = await getMetadata();
  let emotes = metadata.emotes.filter(e => e.status === "approved");
  
  // Apply filters
  if (filters.category) {
    emotes = emotes.filter(e => e.category === filters.category);
  }
  if (filters.search) {
    const search = filters.search.toLowerCase();
    emotes = emotes.filter(e => e.name.toLowerCase().includes(search));
  }
  
  // Apply sort
  if (filters.sort === "name") {
    emotes.sort((a, b) => a.name.localeCompare(b.name));
  } else if (filters.sort === "oldest") {
    emotes.sort((a, b) => new Date(a.createdAt).getTime() - new Date(b.createdAt).getTime());
  } else {
    emotes.sort((a, b) => new Date(b.createdAt).getTime() - new Date(a.createdAt).getTime());
  }
  
  const total = emotes.length;
  const start = (filters.page - 1) * filters.limit;
  const paginated = emotes.slice(start, start + filters.limit);
  
  return {
    emotes: paginated,
    total,
    page: filters.page,
    limit: filters.limit,
    hasMore: start + filters.limit < total,
  };
}

export async function getCategories(): Promise<string[]> {
  const metadata = await getMetadata();
  return metadata.categories;
}
```

### 11.3 Server: app/api/emotes/pending/route.ts

```typescript
import { NextRequest, NextResponse } from "next/server";
import { requireAuth } from "@/lib/auth";
import { savePendingEmote, getPendingEmotes } from "@/lib/db";

export const POST = requireAuth(async (request: NextRequest) => {
  try {
    const body = await request.json();
    const { name, category, fileData, filename, format, size, uploadedBy, uploadedById } = body;
    
    // Validation
    if (!name || !category || !fileData || !uploadedBy) {
      return NextResponse.json(
        { error: "Missing required fields" },
        { status: 400 }
      );
    }
    
    const pendingId = await savePendingEmote({
      name,
      category,
      fileData,
      filename,
      format: format || "png",
      size: size || 0,
      uploadedBy,
      uploadedById,
    });
    
    return NextResponse.json({
      success: true,
      pendingId,
      status: "pending",
    });
  } catch (error) {
    console.error("Error saving pending emote:", error);
    return NextResponse.json(
      { error: "Failed to save pending emote" },
      { status: 500 }
    );
  }
});

export const GET = requireAuth(async (request: NextRequest) => {
  try {
    const { searchParams } = new URL(request.url);
    const page = Math.max(1, parseInt(searchParams.get("page") || "1"));
    const limit = Math.min(100, Math.max(1, parseInt(searchParams.get("limit") || "20")));
    
    const result = await getPendingEmotes({ page, limit });
    
    return NextResponse.json({
      ...result,
      page,
      limit,
    });
  } catch (error) {
    console.error("Error listing pending emotes:", error);
    return NextResponse.json(
      { error: "Failed to list pending emotes" },
      { status: 500 }
    );
  }
});
```

## 12. Testing Checklist

- [ ] Subir emote directo (/addemotes) - admin
- [ ] Crear categoria (/crearcategoria)
- [ ] Enviar emote via formulario (usuario)
- [ ] Ver emote en cola de pendientes (mod)
- [ ] Aprobar emote (mod)
- [ ] Rechazar emote (mod)
- [ ] Listar emotes (/emotes) - paginacion
- [ ] Filtrar por categoria
- [ ] Buscar por nombre
- [ ] Navegar paginas con botones
- [ ] Verificar persistencia tras reinicio
- [ ] Verificar notificaciones DM
- [ ] Verificar broadcast de aprobacion
- [ ] Probar rate limits
- [ ] Probar con archivos grandes (>5MB debe fallar)
- [ ] Probar con formatos invalidos
- [ ] Probar duplicados de nombre

## 13. Proximos Pasos

1. **Aprobar este plan** y decidir opciones de storage
2. **Implementar servidor Vercel** (Fase 1)
3. **Actualizar bot** con nuevos metodos API (Fase 2)
4. **Testing integrado** en servidor de desarrollo
5. **Deploy a produccion** (Fase 4)

---

**Nota:** El sistema actual del bot ya tiene una base solida. La mayor parte del trabajo esta en implementar el servidor Vercel y conectarlo correctamente. El flujo de submissions con modal, botones de moderacion y notificaciones ya funciona y solo necesita persistencia remota.
