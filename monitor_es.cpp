/*
=======================================================================
  MONITOR E/S EN TIEMPO REAL - Tanenbaum Cap. 5
  Alumno : Carlos | Ryzen 7 7445HS | Windows 11

  ANTI-PARPADEO: doble buffer de celdas. Solo se escribe en consola
  la diferencia entre el frame anterior y el actual -> cero flickering.

  COMPILE:
    g++ -O0 -std=c++14 -o monitor_es.exe monitor_es.cpp -lpsapi
  RUN:
    monitor_es.exe
  NOTA: consola 100x35 - maximizar ventana si es necesario
=======================================================================
*/
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#ifndef WINVER
#define WINVER 0x0501
#endif
#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#pragma comment(lib,"psapi.lib")
#include <tlhelp32.h>
#include <setupapi.h>
#pragma comment(lib,"setupapi.lib")

// MOUSE_HWHEELED puede no estar definido en MinGW antiguo
#ifndef MOUSE_HWHEELED
#define MOUSE_HWHEELED 0x0008
#endif
#ifndef WHEEL_DELTA
#define WHEEL_DELTA 120
#endif

// ================================================================
//  DOBLE BUFFER - la clave para eliminar el parpadeo
//  Guardamos caracter + atributo de color por cada celda.
//  render() escribe en el buffer virtual; flush_buffer() solo
//  manda a la consola las celdas que cambiaron respecto al frame anterior.
// ================================================================
static const int BUF_W = 100;
static const int BUF_H = 35;

struct Celda { char ch; WORD attr; };

static Celda buf_actual  [BUF_W * BUF_H];
static Celda buf_anterior[BUF_W * BUF_H];
static HANDLE hCon;
static HANDLE hIn;
static int    CON_W = BUF_W;
static int    CON_H = BUF_H;

static const WORD CYN=11,AMR=14,VRD=10,ROJ=12,BLC=15,GRI=8,MGA=13,AZL=9;

void buf_clear(){
    for(int i=0;i<BUF_W*BUF_H;i++){
        buf_actual[i].ch   = ' ';
        buf_actual[i].attr = 7;
    }
}

void buf_put(int x, int y, char ch, WORD attr){
    if(x<0||x>=BUF_W||y<0||y>=BUF_H) return;
    buf_actual[y*BUF_W+x].ch   = ch;
    buf_actual[y*BUF_W+x].attr = attr;
}

void buf_str(int x, int y, WORD attr, const std::string& s){
    for(int i=0;i<(int)s.size();i++) buf_put(x+i,y,s[i],attr);
}
void buf_str(int x, int y, WORD attr, const char* s){ buf_str(x,y,attr,std::string(s)); }

// Solo escribe en consola las celdas que cambiaron
void flush_buffer(){
    CONSOLE_CURSOR_INFO ci={1,FALSE};
    SetConsoleCursorInfo(hCon,&ci);
    WORD last_attr=0xFFFF;
    for(int row=0;row<BUF_H;row++){
        for(int col=0;col<BUF_W;col++){
            int idx=row*BUF_W+col;
            Celda& a=buf_actual[idx];
            Celda& p=buf_anterior[idx];
            if(a.ch==p.ch && a.attr==p.attr) continue;
            COORD c={(SHORT)col,(SHORT)row};
            SetConsoleCursorPosition(hCon,c);
            if(a.attr!=last_attr){
                SetConsoleTextAttribute(hCon,a.attr);
                last_attr=a.attr;
            }
            DWORD w; WriteConsoleA(hCon,&a.ch,1,&w,NULL);
            p=a;
        }
    }
    SetConsoleTextAttribute(hCon,7);
}

// ================================================================
//  HELPERS DE DIBUJO
// ================================================================
void bstr(int x, int y, WORD col, const std::string& s){ buf_str(x,y,col,s); }
void bstr(int x, int y, WORD col, const char* s)       { buf_str(x,y,col,s); }

void bhline(int x, int y, int len, char c='-', WORD col=GRI){
    for(int i=0;i<len;i++) buf_put(x+i,y,c,col);
}

void bbox(int x, int y, int w, int h, WORD col, const char* titulo=""){
    buf_put(x,    y,    '+',col); buf_put(x+w-1,y,    '+',col);
    buf_put(x,    y+h-1,'+',col); buf_put(x+w-1,y+h-1,'+',col);
    for(int i=1;i<w-1;i++){
        buf_put(x+i,y,    '-',col);
        buf_put(x+i,y+h-1,'-',col);
    }
    for(int j=1;j<h-1;j++){
        buf_put(x,    y+j,'|',col);
        buf_put(x+w-1,y+j,'|',col);
    }
    if(titulo&&titulo[0]){
        std::string t=std::string("[")+titulo+"]";
        buf_str(x+2,y,AMR,t);
    }
}

std::string fw(long long v, int w=8){
    std::ostringstream s; s<<std::setw(w)<<v; return s.str();
}
std::string fhex(DWORD v, int digits=8){
    std::ostringstream s;
    s<<"0x"<<std::hex<<std::uppercase<<std::setw(digits)<<std::setfill('0')<<v;
    return s.str();
}
std::string fpct(double v){
    std::ostringstream s;
    s<<std::fixed<<std::setprecision(1)<<std::setw(5)<<v<<"%"; return s.str();
}
std::string fmb(ULONGLONG b){
    std::ostringstream s;
    s<<std::fixed<<std::setprecision(1)<<(double)b/(1024.0*1024.0)<<"MB"; return s.str();
}
std::string barra(double pct, int w=20){
    if(pct<0)pct=0; if(pct>100)pct=100;
    int f=(int)(pct*w/100);
    std::string r="["; for(int i=0;i<w;i++) r+=(i<f?'#':' '); return r+"]";
}

// ================================================================
//  ESTADO GLOBAL
// ================================================================
struct Estado {
    int   mouse_x=0,mouse_y=0,mouse_dx=0,mouse_dy=0;
    bool  btn_izq=false,btn_der=false,btn_mid=false;
    bool  btn_x1=false,btn_x2=false;       // botones laterales (XButton)
    int   wheel_delta=0;                    // rueda principal (trackball vertical)
    int   hwheel_delta=0;                   // rueda horizontal (trackball horizontal)
    long long wheel_total=0;                // giros acumulados rueda
    long long hwheel_total=0;
    BYTE  reg_estado_mouse=0x08,reg_datos_mouse=0;
    BYTE  reg_wheel_mouse=0;                // Byte4 PS/2 extendido (IntelliMouse)
    long long mouse_eventos=0;

    BYTE  ultimo_scancode=0; char ultimo_char=0; WORD ultimo_vk=0;
    BYTE  reg_estado_kbd=0x14,reg_datos_kbd=0;
    bool  caps=false,num=false,scroll=false,shift=false,ctrl=false,alt=false;
    bool  ins_activo=false;                 // estado INSERT toggle
    long long kbd_eventos=0;
    // Teclas especiales de ordenador (ultimo estado)
    bool  tk_insert=false,tk_delete=false,tk_home=false,tk_end=false;
    bool  tk_pgup=false,tk_pgdn=false,tk_pause=false,tk_prtscr=false;
    bool  tk_winl=false,tk_winr=false,tk_apps=false;
    WORD  ultima_tk_especial=0;             // VK de la ultima tecla especial

    ULONGLONG bytes_leidos=0,bytes_escritos=0;
    double vel_lect_mb=0,vel_escr_mb=0;
    DWORD  dma_dir_base=0x00100000,dma_contador=0;
    BYTE   dma_modo=0x58,dma_estado=0x0F;
    DWORD  irq14_count=0;

    double    cpu_pct=0;
    DWORD     irq0_count=0,pit_counter0=0,tick_actual=0;
    ULONGLONG uptime_ms=0;

    SIZE_T    ws_actual=0,ws_peak=0;
    DWORD     pf_total=0,pf_delta=0,pf_ant=0;
    ULONGLONG ram_total=0,ram_libre=0;
    DWORD     pct_ram=0;
    ULONGLONG pf_sys_total=0;
    DWORD     dma_pf_activos=0;

    struct Canal {
        std::string dispositivo;
        DWORD  dir_base=0,contador=0;
        BYTE   modo=0;
        bool   activo=false,terminal_count=false;
        DWORD  transferencias=0;
    } canales[8];

    DWORD irq_por_segundo=0,frame=0;

    // E/S del SISTEMA COMPLETO (todos los procesos, via PDH)
    double sys_disk_read_bps=0;    // bytes/s lectura sistema
    double sys_disk_write_bps=0;   // bytes/s escritura sistema
    double sys_disk_rops=0;        // operaciones lectura/s
    double sys_disk_wops=0;        // operaciones escritura/s
    double sys_cpu_pct=0;          // CPU total sistema %
    ULONGLONG sys_bytes_leidos_total=0;
    ULONGLONG sys_bytes_escritos_total=0;

    // Monitor de cambios de archivos (ReadDirectoryChangesW)
    struct EventoArchivo {
        char tipo[12];   // "CREADO","BORRADO","MOVIDO","MODIF."
        char nombre[60];
        DWORD tick;
    };
    static const int MAX_EVENTOS = 10;
    EventoArchivo eventos_arch[MAX_EVENTOS];
    int n_eventos=0;
    DWORD ultimo_evento_tick=0;

    // --- Panel SISTEMA ---
    DWORD  ventanas_totales=0;
    DWORD  ventanas_visibles=0;
    DWORD  procesos_total=0;
    DWORD  hilos_total=0;
    DWORD  handles_proceso=0;
    ULONGLONG bytes_commit=0;      // memoria comprometida sistema
    // Drivers (hasta 12 entradas)
    struct InfoDriver {
        char nombre[32];
        char base[12];
        char tam[10];
    } drivers[12];
    int n_drivers=0;

    // --- Panel DISCO DETALLADO ---
    ULONGLONG disco_total_bytes=0;
    ULONGLONG disco_libre_bytes=0;
    ULONGLONG disco_usado_bytes=0;
    DWORD  disco_sectores_cluster=0;
    DWORD  disco_bytes_sector=0;
    DWORD  disco_clusters_libres=0;
    DWORD  disco_clusters_total=0;
    char   disco_fs[16]="";        // FAT32/NTFS/exFAT
    char   disco_volumen[32]="";
    char   disco_letra='C';
    DWORD  disco_serial=0;
    ULONGLONG bytes_leidos_total_ant=0;
    double iops_lect=0,iops_escr=0;
    DWORD  io_read_ops_ant=0,io_write_ops_ant=0;
    DWORD  io_read_ops=0,io_write_ops=0;
};

// ================================================================
//  CPU via GetProcessTimes (compatible MinGW32)
// ================================================================
static ULONGLONG cpu_kern_ant=0,cpu_user_ant=0,cpu_wall_ant=0;

// ================================================================
//  E/S DEL SISTEMA COMPLETO sin PDH
//  Usa NtQuerySystemInformation (ntdll, siempre disponible)
//  SystemPerformanceInformation (clase 2) contiene bytes leidos/escritos
//  del sistema completo desde boot.
// ================================================================
typedef LONG (WINAPI* PNtQSI)(UINT,PVOID,ULONG,PULONG);
static PNtQSI pNtQSI = NULL;

// SYSTEM_PERFORMANCE_INFORMATION - campos relevantes (offsets fijos NT)
// La estructura tiene >100 campos; solo necesitamos los primeros bytes
#pragma pack(push,1)
struct SYSPERF {
    LARGE_INTEGER IdleProcessTime;
    LARGE_INTEGER IoReadTransferCount;   // bytes leidos  sistema total
    LARGE_INTEGER IoWriteTransferCount;  // bytes escritos sistema total
    LARGE_INTEGER IoOtherTransferCount;
    ULONG IoReadOperationCount;
    ULONG IoWriteOperationCount;
    ULONG IoOtherOperationCount;
    // ... muchos mas campos que no usamos
    BYTE  resto[512];
};
#pragma pack(pop)

// CPU sistema via GetSystemTimes (disponible desde XP, sin dependencias)
static FILETIME cpu_idle_ant={0},cpu_kern_sys_ant={0},cpu_user_sys_ant={0};

void init_sysio(){
    // Cargar NtQuerySystemInformation dinamicamente de ntdll
    HMODULE hNt=GetModuleHandleA("ntdll.dll");
    if(hNt) pNtQSI=(PNtQSI)GetProcAddress(hNt,"NtQuerySystemInformation");
    // Primera muestra CPU sistema
    GetSystemTimes(&cpu_idle_ant,&cpu_kern_sys_ant,&cpu_user_sys_ant);
}

struct DatosSys {
    double disk_read_bps=0;
    double disk_write_bps=0;
    double disk_read_ops=0;
    double disk_write_ops=0;
    double cpu_sistema=0;
};

static SYSPERF perf_ant={0};
static bool    perf_ant_valido=false;
static DWORD   perf_tick_ant=0;

DatosSys leer_sysio(){
    DatosSys d;
    // --- E/S del sistema ---
    if(pNtQSI){
        SYSPERF perf; ULONG ret=0;
        // clase 2 = SystemPerformanceInformation
        LONG st=pNtQSI(2,&perf,sizeof(perf),&ret);
        if(st==0 && perf_ant_valido){
            DWORD now=GetTickCount();
            double dt=(now-perf_tick_ant)/1000.0;
            if(dt>0.01){
                d.disk_read_bps =(double)(perf.IoReadTransferCount.QuadPart -perf_ant.IoReadTransferCount.QuadPart )/dt;
                d.disk_write_bps=(double)(perf.IoWriteTransferCount.QuadPart-perf_ant.IoWriteTransferCount.QuadPart)/dt;
                d.disk_read_ops =(double)(perf.IoReadOperationCount -perf_ant.IoReadOperationCount )/dt;
                d.disk_write_ops=(double)(perf.IoWriteOperationCount-perf_ant.IoWriteOperationCount)/dt;
            }
        }
        if(st==0){ perf_ant=perf; perf_ant_valido=true; perf_tick_ant=GetTickCount(); }
    }
    // --- CPU del sistema via GetSystemTimes ---
    {
        FILETIME idle,kern,user;
        GetSystemTimes(&idle,&kern,&user);
        ULARGE_INTEGER ui,uk,uu,pi,pk,pu;
        ui.LowPart=idle.dwLowDateTime; ui.HighPart=idle.dwHighDateTime;
        uk.LowPart=kern.dwLowDateTime; uk.HighPart=kern.dwHighDateTime;
        uu.LowPart=user.dwLowDateTime; uu.HighPart=user.dwHighDateTime;
        pi.LowPart=cpu_idle_ant.dwLowDateTime; pi.HighPart=cpu_idle_ant.dwHighDateTime;
        pk.LowPart=cpu_kern_sys_ant.dwLowDateTime; pk.HighPart=cpu_kern_sys_ant.dwHighDateTime;
        pu.LowPart=cpu_user_sys_ant.dwLowDateTime; pu.HighPart=cpu_user_sys_ant.dwHighDateTime;
        ULONGLONG d_idle=ui.QuadPart-pi.QuadPart;
        ULONGLONG d_kern=uk.QuadPart-pk.QuadPart;
        ULONGLONG d_user=uu.QuadPart-pu.QuadPart;
        ULONGLONG total=d_kern+d_user;
        if(total>0) d.cpu_sistema=100.0*(1.0-(double)d_idle/(double)total);
        cpu_idle_ant=idle; cpu_kern_sys_ant=kern; cpu_user_sys_ant=user;
    }
    return d;
}
void init_cpu(){
    FILETIME cr,ex,kr,us;
    GetProcessTimes(GetCurrentProcess(),&cr,&ex,&kr,&us);
    ULARGE_INTEGER uk,uu;
    uk.LowPart=kr.dwLowDateTime; uk.HighPart=kr.dwHighDateTime;
    uu.LowPart=us.dwLowDateTime; uu.HighPart=us.dwHighDateTime;
    cpu_kern_ant=uk.QuadPart; cpu_user_ant=uu.QuadPart;
    cpu_wall_ant=(ULONGLONG)GetTickCount()*10000ULL;
}
double leer_cpu(){
    FILETIME cr,ex,kr,us;
    GetProcessTimes(GetCurrentProcess(),&cr,&ex,&kr,&us);
    ULARGE_INTEGER uk,uu;
    uk.LowPart=kr.dwLowDateTime; uk.HighPart=kr.dwHighDateTime;
    uu.LowPart=us.dwLowDateTime; uu.HighPart=us.dwHighDateTime;
    ULONGLONG kern=uk.QuadPart,user=uu.QuadPart;
    ULONGLONG wall=(ULONGLONG)GetTickCount()*10000ULL;
    ULONGLONG d_cpu=(kern-cpu_kern_ant)+(user-cpu_user_ant);
    ULONGLONG d_wall=wall-cpu_wall_ant;
    cpu_kern_ant=kern; cpu_user_ant=user; cpu_wall_ant=wall;
    if(!d_wall) return 0.0;
    return std::min(100.0,100.0*(double)d_cpu/(double)d_wall);
}

// ================================================================
//  HELPERS PARA SISTEMA
// ================================================================
struct ContVentanas { DWORD total; DWORD visibles; };
static BOOL CALLBACK enumWndCb(HWND hw, LPARAM lp){
    ContVentanas* cv=(ContVentanas*)lp;
    cv->total++;
    if(IsWindowVisible(hw)) cv->visibles++;
    return TRUE;
}

// ================================================================
//  MONITOR DE CAMBIOS DE ARCHIVOS (hilo separado)
//  ReadDirectoryChangesW detecta crear/borrar/renombrar/modificar
// ================================================================
static Estado* g_estado_ptr = NULL;
static CRITICAL_SECTION g_cs;

DWORD WINAPI hilo_archivos(LPVOID){
    char ruta[MAX_PATH];
    if(!GetEnvironmentVariableA("USERPROFILE",ruta,MAX_PATH))
        strcpy(ruta,"C:\\Users");

    HANDLE hDir=CreateFileA(ruta,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED,NULL);
    if(hDir==INVALID_HANDLE_VALUE) return 0;

    BYTE buf[8192]; DWORD br=0;
    OVERLAPPED ov={0};
    ov.hEvent=CreateEvent(NULL,TRUE,FALSE,NULL);

    while(true){
        ResetEvent(ov.hEvent);
        ReadDirectoryChangesW(hDir,(LPVOID)buf,sizeof(buf),TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|
            FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_LAST_WRITE,
            &br,&ov,NULL);
        if(WaitForSingleObject(ov.hEvent,500)!=WAIT_OBJECT_0) continue;
        if(!GetOverlappedResult(hDir,&ov,&br,FALSE)||br==0) continue;

        FILE_NOTIFY_INFORMATION* fni=(FILE_NOTIFY_INFORMATION*)buf;
        EnterCriticalSection(&g_cs);
        do {
            const char* tipo="MODIF. ";
            switch(fni->Action){
                case FILE_ACTION_ADDED:            tipo="CREADO "; break;
                case FILE_ACTION_REMOVED:          tipo="BORRADO"; break;
                case FILE_ACTION_RENAMED_OLD_NAME: tipo="MOV-DE "; break;
                case FILE_ACTION_RENAMED_NEW_NAME: tipo="MOV-A  "; break;
            }
            char nombre[60]="";
            int len=(int)(fni->FileNameLength/sizeof(WCHAR));
            if(len>58)len=58;
            WideCharToMultiByte(CP_ACP,0,fni->FileName,len,nombre,59,NULL,NULL);
            if(g_estado_ptr){
                Estado& e=*g_estado_ptr;
                if(e.n_eventos>=Estado::MAX_EVENTOS){
                    for(int i=0;i<Estado::MAX_EVENTOS-1;i++)
                        e.eventos_arch[i]=e.eventos_arch[i+1];
                    e.n_eventos=Estado::MAX_EVENTOS-1;
                }
                strncpy(e.eventos_arch[e.n_eventos].tipo,tipo,11);
                strncpy(e.eventos_arch[e.n_eventos].nombre,nombre,59);
                e.eventos_arch[e.n_eventos].tick=GetTickCount();
                e.n_eventos++;
                e.ultimo_evento_tick=GetTickCount();
            }
            if(!fni->NextEntryOffset) break;
            fni=(FILE_NOTIFY_INFORMATION*)((BYTE*)fni+fni->NextEntryOffset);
        } while(true);
        LeaveCriticalSection(&g_cs);
    }
    CloseHandle(hDir);
    return 0;
}

// ================================================================
//  ACTUALIZAR ESTADO
// ================================================================
void actualizar(Estado& e){
    e.frame++;
    POINT pt; GetCursorPos(&pt);
    e.mouse_dx=pt.x-e.mouse_x; e.mouse_dy=pt.y-e.mouse_y;
    e.mouse_x=pt.x; e.mouse_y=pt.y;
    e.btn_izq=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
    e.btn_der=(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;
    e.btn_mid=(GetAsyncKeyState(VK_MBUTTON)&0x8000)!=0;
    e.btn_x1 =(GetAsyncKeyState(VK_XBUTTON1)&0x8000)!=0;
    e.btn_x2 =(GetAsyncKeyState(VK_XBUTTON2)&0x8000)!=0;
    e.reg_estado_mouse=0x08;
    if(e.btn_izq) e.reg_estado_mouse|=0x01;
    if(e.btn_der) e.reg_estado_mouse|=0x02;
    if(e.btn_mid) e.reg_estado_mouse|=0x04;
    if(e.mouse_dx<0) e.reg_estado_mouse|=0x10;
    if(e.mouse_dy<0) e.reg_estado_mouse|=0x20;
    if(abs(e.mouse_dx)>127||abs(e.mouse_dy)>127) e.reg_estado_mouse|=0xC0;
    e.reg_datos_mouse=(BYTE)(e.mouse_dx&0xFF);
    // Byte4 PS/2 IntelliMouse: bits[3:0] wheel, bit4=X1, bit5=X2
    e.reg_wheel_mouse = (BYTE)(e.wheel_delta & 0x0F);
    if(e.btn_x1) e.reg_wheel_mouse |= 0x10;
    if(e.btn_x2) e.reg_wheel_mouse |= 0x20;
    if(e.mouse_dx||e.mouse_dy||e.btn_izq||e.btn_der||e.wheel_delta) e.mouse_eventos++;

    // Modificadores leidos via dwControlKeyState en capturar_input()
    // (mas confiable que GetKeyboardState para consolas)
    // Solo actualizar reg_datos_kbd con el ultimo scancode registrado
    e.reg_datos_kbd=e.ultimo_scancode;

    // --- E/S del SISTEMA via PDH (refleja cualquier proceso, incl. Explorer) ---
    {
        DatosSys pdh = leer_sysio();
        e.sys_disk_read_bps  = pdh.disk_read_bps;
        e.sys_disk_write_bps = pdh.disk_write_bps;
        e.sys_disk_rops      = pdh.disk_read_ops;
        e.sys_disk_wops      = pdh.disk_write_ops;
        e.sys_cpu_pct        = pdh.cpu_sistema;
        // MB/s para las barras existentes (ahora sistema completo)
        e.vel_lect_mb  = pdh.disk_read_bps  / (1024.0*1024.0);
        e.vel_escr_mb  = pdh.disk_write_bps / (1024.0*1024.0);
        // Acumulados aproximados
        e.sys_bytes_leidos_total  += (ULONGLONG)pdh.disk_read_bps  / 5;
        e.sys_bytes_escritos_total+= (ULONGLONG)pdh.disk_write_bps / 5;
        e.bytes_leidos  = e.sys_bytes_leidos_total;
        e.bytes_escritos= e.sys_bytes_escritos_total;
    }
    // E/S del propio proceso (para panel DMA)
    IO_COUNTERS ioc;
    GetProcessIoCounters(GetCurrentProcess(),&ioc);
    e.dma_dir_base=0x00100000+(DWORD)(e.bytes_leidos&0x0FFFFFFF);
    e.dma_contador=(e.vel_lect_mb>0.1)?(DWORD)(e.vel_lect_mb*1024.0*1024.0/5.0):0;
    e.dma_modo=0x58;
    e.dma_estado=(e.dma_contador>0)?0x04:0x0F;
    if(e.vel_lect_mb>0.5||e.vel_escr_mb>0.5) e.irq14_count++;

    e.cpu_pct=leer_cpu();
    e.tick_actual=GetTickCount();
    e.uptime_ms=e.tick_actual;
    e.irq0_count=e.tick_actual;
    e.pit_counter0=(DWORD)(11932-(e.tick_actual%11932));

    PROCESS_MEMORY_COUNTERS pmc; pmc.cb=sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(),&pmc,sizeof(pmc));
    e.pf_delta=pmc.PageFaultCount-e.pf_ant;
    e.pf_ant=pmc.PageFaultCount;
    e.pf_total=pmc.PageFaultCount;
    e.ws_actual=pmc.WorkingSetSize; e.ws_peak=pmc.PeakWorkingSetSize;
    MEMORYSTATUSEX ms; ms.dwLength=sizeof(ms); GlobalMemoryStatusEx(&ms);
    e.ram_total=ms.ullTotalPhys; e.ram_libre=ms.ullAvailPhys;
    e.pct_ram=ms.dwMemoryLoad;
    e.pf_sys_total=ms.ullTotalPageFile-ms.ullAvailPageFile;
    e.dma_pf_activos=e.pf_delta*5;

    const char* dn[]={"Mem->Mem","Audio/ISA","Disquete","LPT1","Cascade","ISA-16b","ISA-16b","ISA-16b"};
    const BYTE  dm[]={0x40,0x41,0x42,0x43,0x44,0x49,0x4A,0x4B};
    for(int i=0;i<8;i++){
        e.canales[i].dispositivo=dn[i]; e.canales[i].modo=dm[i];
        bool act=false;
        if(i==1&&e.frame%15<4) act=true;
        if(i==2&&e.vel_lect_mb>0.1) act=true;
        if(i==4) act=true;
        e.canales[i].activo=act;
        e.canales[i].terminal_count=!act&&i!=4;
        e.canales[i].dir_base=0x00100000+(DWORD)(i*0x00200000);
        e.canales[i].contador=act?(DWORD)(e.vel_lect_mb*102400+i*512):0;
        if(act) e.canales[i].transferencias++;
    }
    e.irq_por_segundo=(DWORD)(e.irq0_count%1000)+e.irq14_count%100+e.pf_delta*5;

    // --- Ventanas y procesos ---
    ContVentanas cv={0,0};
    EnumWindows(enumWndCb,(LPARAM)&cv);
    e.ventanas_totales=cv.total;
    e.ventanas_visibles=cv.visibles;
    {
        HANDLE hSnap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS|TH32CS_SNAPTHREAD,0);
        if(hSnap!=INVALID_HANDLE_VALUE){
            PROCESSENTRY32 pe; pe.dwSize=sizeof(pe);
            DWORD np=0,nt=0;
            if(Process32First(hSnap,&pe)) do { np++; } while(Process32Next(hSnap,&pe));
            THREADENTRY32 te; te.dwSize=sizeof(te);
            if(Thread32First(hSnap,&te)) do { nt++; } while(Thread32Next(hSnap,&te));
            e.procesos_total=np; e.hilos_total=nt;
            CloseHandle(hSnap);
        }
    }
    {
        // GetProcessHandleCount puede no existir en MinGW antiguo
        // Alternativa: leer desde PROCESS_MEMORY_COUNTERS_EX
        PROCESS_MEMORY_COUNTERS_EX pmcx;
        pmcx.cb=sizeof(pmcx);
        if(GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&pmcx,sizeof(pmcx))){
            // Usar HandleCount desde toolhelp como alternativa
            HANDLE hSnap2=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
            if(hSnap2!=INVALID_HANDLE_VALUE){
                PROCESSENTRY32 pe2; pe2.dwSize=sizeof(pe2);
                DWORD pid=GetCurrentProcessId();
                if(Process32First(hSnap2,&pe2)) do {
                    if(pe2.th32ProcessID==pid){
                        e.handles_proceso=(DWORD)pe2.cntThreads; // hilos de este proceso
                        break;
                    }
                } while(Process32Next(hSnap2,&pe2));
                CloseHandle(hSnap2);
            }
        }
    }
    {
        MEMORYSTATUSEX ms2; ms2.dwLength=sizeof(ms2); GlobalMemoryStatusEx(&ms2);
        e.bytes_commit=ms2.ullTotalPageFile-ms2.ullAvailPageFile;
    }

    // --- Drivers del kernel (modulos cargados en sistema) ---
    if(e.frame%25==0){  // actualizar cada 5 segundos
        e.n_drivers=0;
        HMODULE mods[256]; DWORD cb=0;
        // EnumDeviceDrivers lista modulos del kernel
        if(EnumDeviceDrivers((LPVOID*)mods,sizeof(mods),&cb)){
            int nm=(int)(cb/sizeof(HMODULE));
            int lim=(nm<12)?nm:12;
            for(int i=0;i<lim;i++){
                char buf[64]="";
                GetDeviceDriverBaseNameA(mods[i],buf,sizeof(buf));
                strncpy(e.drivers[e.n_drivers].nombre,buf,31);
                // base address
                std::ostringstream oss;
                oss<<"0x"<<std::hex<<std::uppercase<<(ULONG_PTR)mods[i];
                strncpy(e.drivers[e.n_drivers].base,oss.str().c_str(),11);
                e.n_drivers++;
            }
        }
    }

    // --- Disco detallado (unidad C:) ---
    {
        ULARGE_INTEGER tot,lib,totl;
        if(GetDiskFreeSpaceExA("C:\\",&lib,&tot,&totl)){
            e.disco_total_bytes=tot.QuadPart;
            e.disco_libre_bytes=lib.QuadPart;
            e.disco_usado_bytes=tot.QuadPart-lib.QuadPart;
        }
        DWORD spc=0,bps=0,fc=0,tc=0;
        if(GetDiskFreeSpaceA("C:\\",&spc,&bps,&fc,&tc)){
            e.disco_sectores_cluster=spc;
            e.disco_bytes_sector=bps;
            e.disco_clusters_libres=fc;
            e.disco_clusters_total=tc;
        }
        char volNom[32]="",fsNom[16]="";
        DWORD serial=0;
        GetVolumeInformationA("C:\\",volNom,32,&serial,NULL,NULL,fsNom,16);
        strncpy(e.disco_volumen,volNom,31);
        strncpy(e.disco_fs,fsNom,15);
        e.disco_serial=serial;
        // IOPS aproximados desde IO_COUNTERS
        IO_COUNTERS ioc2;
        if(GetProcessIoCounters(GetCurrentProcess(),&ioc2)){
            DWORD rops=(DWORD)ioc2.ReadOperationCount;
            DWORD wops=(DWORD)ioc2.WriteOperationCount;
            e.iops_lect=(double)(rops-e.io_read_ops_ant)*5.0;
            e.iops_escr=(double)(wops-e.io_write_ops_ant)*5.0;
            e.io_read_ops_ant=rops; e.io_write_ops_ant=wops;
            e.io_read_ops=rops; e.io_write_ops=wops;
        }
    }
}

// ================================================================
//  PANELES
// ================================================================
static const char* PNAMES[]={"RATON+TRACKBALL","TECLADO i8042","DISCO+DMA","CPU+TIMER","MEMORIA","CANALES DMA","SISTEMA/DRIVERS","DISCO DETALLE"};
static const int   NPANELES=8;

void render_cabecera(const Estado& e, int panel){
    bstr(0,0,AMR,"  MONITOR E/S TIEMPO REAL | Tanenbaum Cap.5 | Carlos | 100x35");
    SYSTEMTIME st; GetLocalTime(&st);
    std::ostringstream ts;
    ts<<std::setw(2)<<std::setfill('0')<<st.wHour<<":"
      <<std::setw(2)<<std::setfill('0')<<st.wMinute<<":"
      <<std::setw(2)<<std::setfill('0')<<st.wSecond;
    bstr(91,0,CYN,ts.str());
    bhline(0,1,BUF_W,'=',AMR);
    std::ostringstream nav;
    nav<<"  Panel ["<<(panel+1)<<"/"<<NPANELES<<"] "<<PNAMES[panel];
    bstr(0,2,AMR,nav.str());
    bstr(55,2,GRI,"[->]=Sig [<-]=Ant [F1-F8]=Panel [Q]=Sal");
    bhline(0,3,BUF_W,'-',GRI);
}

void render_pie(const Estado& e){
    bhline(0,BUF_H-4,BUF_W,'=',GRI);
    bstr(1,BUF_H-3,GRI,"IRQ0 Timer:"); bstr(13,BUF_H-3,CYN,fw(e.irq0_count%100000,7));
    bstr(21,BUF_H-3,GRI," IRQ1 KBD:"); bstr(32,BUF_H-3,AMR,fw(e.kbd_eventos,6));
    bstr(39,BUF_H-3,GRI," IRQ12 Raton:"); bstr(53,BUF_H-3,MGA,fw(e.mouse_eventos,6));
    bstr(60,BUF_H-3,GRI," IRQ14 IDE:"); bstr(72,BUF_H-3,VRD,fw(e.irq14_count,5));
    bstr(78,BUF_H-3,GRI," CPU:"); bstr(84,BUF_H-3,(e.cpu_pct>80?ROJ:e.cpu_pct>50?AMR:VRD),fpct(e.cpu_pct));
    bstr(1,BUF_H-2,GRI,"PF/s:"); bstr(7,BUF_H-2,ROJ,fw(e.dma_pf_activos,5));
    bstr(13,BUF_H-2,GRI," IRQ/s:"); bstr(21,BUF_H-2,ROJ,fw(e.irq_por_segundo,5));
    bstr(27,BUF_H-2,GRI," Frame:"); bstr(35,BUF_H-2,BLC,fw(e.frame,5));
    bstr(41,BUF_H-2,GRI," RAM%:"); bstr(48,BUF_H-2,(e.pct_ram>85?ROJ:AMR),fw(e.pct_ram,3));
    bstr(52,BUF_H-2,GRI,"  Wheel:"); bstr(61,BUF_H-2,CYN,fw(e.wheel_total,5));
    bstr(67,BUF_H-2,GRI," KBDevt:"); bstr(76,BUF_H-2,AMR,fw(e.kbd_eventos,6));
    bstr(83,BUF_H-2,GRI," Ins:"); bstr(89,BUF_H-2,e.ins_activo?CYN:GRI,e.ins_activo?"ON":"--");
    bhline(0,BUF_H-1,BUF_W,'-',GRI);
}

// ---- PANEL 1: RATON ----
void render_raton(const Estado& e){
    bbox(0,4,BUF_W,BUF_H-8,CYN,"RATON PS/2 IntelliMouse - Controlador PS/2");

    bstr(2,5,AMR,"[Posicion y movimiento]");
    bstr(2,6,GRI,"Posicion X   :"); bstr(17,6,VRD,fw(e.mouse_x,6)); bstr(24,6,GRI,"px  pantalla");
    bstr(2,7,GRI,"Posicion Y   :"); bstr(17,7,VRD,fw(e.mouse_y,6)); bstr(24,7,GRI,"px  pantalla");
    bstr(2,8,GRI,"Delta dX     :"); bstr(17,8,(e.mouse_dx?AMR:GRI),fw(e.mouse_dx,6));
    bstr(24,8,GRI,"  Byte2 PS/2:"); bstr(38,8,CYN,fhex((DWORD)(e.mouse_dx&0xFF),2));
    bstr(43,8,GRI,"movim. horiz. (complemento a 2)");
    bstr(2,9,GRI,"Delta dY     :"); bstr(17,9,(e.mouse_dy?AMR:GRI),fw(e.mouse_dy,6));
    bstr(24,9,GRI,"  Byte3 PS/2:"); bstr(38,9,CYN,fhex((DWORD)(e.mouse_dy&0xFF),2));
    bstr(43,9,GRI,"movim. vert.  (complemento a 2)");

    bhline(1,10,BUF_W-2,'-',GRI);
    bstr(2,11,AMR,"[TrackBall / Rueda de desplazamiento - IntelliMouse Byte4 PS/2]");
    // Indicador visual de la rueda
    {
        std::string wbar="[";
        int wv = (int)(e.wheel_total % 20); if(wv<0)wv+=20;
        for(int i=0;i<20;i++) wbar += (i==wv ? 'O' : '-');
        wbar += "]";
        WORD wc = (e.wheel_delta>0)?VRD:(e.wheel_delta<0)?ROJ:GRI;
        bstr(2,12,GRI,"Rueda V      :"); bstr(17,12,wc,fw(e.wheel_delta,+4));
        bstr(22,12,GRI,"  (delta/notch) Total:"); bstr(44,12,CYN,fw(e.wheel_total,6));
        bstr(2,13,wc,wbar);
        bstr(24,13,GRI,"<- giro arriba=+120, abajo=-120 (WHEEL_DELTA=120)");
    }
    {
        WORD hwc = (e.hwheel_delta>0)?MGA:(e.hwheel_delta<0)?AMR:GRI;
        bstr(2,14,GRI,"Rueda H      :"); bstr(17,14,hwc,fw(e.hwheel_delta,+4));
        bstr(22,14,GRI,"  Total horiz:"); bstr(36,14,CYN,fw(e.hwheel_total,6));
        bstr(44,14,GRI,"<- rueda lateral o trackball 2D");
    }
    bstr(2,15,GRI,"Byte4 PS/2   :"); bstr(17,15,CYN,fhex(e.reg_wheel_mouse,2));
    bstr(22,15,GRI,"IntelliMouse ext: [7:6]=00 [5]=X2 [4]=X1 [3:0]=wheel");

    bhline(1,16,BUF_W-2,'-',GRI);
    bstr(2,17,AMR,"[Byte1 PS/2 = Registro de Estado - puerto 0x60]");
    bstr(2,18,GRI,"Byte1 = "); bstr(10,18,CYN,fhex(e.reg_estado_mouse,2));
    bstr(15,18,GRI,"  formato: [Yo|Xo|Ys|Xs|1|M|R|L]");

    struct { int bit; WORD con; const char* nom; const char* d0; const char* d1; } bits[]={
        {7,ROJ,"Yo overflow dY","0=dY dentro de rango","1=overflow dY (movimiento rapido)"},
        {6,ROJ,"Xo overflow dX","0=dX dentro de rango","1=overflow dX (movimiento rapido)"},
        {5,AMR,"Ys signo dY   ","0=dY positivo(hacia abajo)","1=dY negativo(hacia arriba)"},
        {4,AMR,"Xs signo dX   ","0=dX positivo(hacia derecha)","1=dX negativo(hacia izquierda)"},
        {3,VRD,"Siempre 1     ","NUNCA es 0","1 = sincroniza inicio paquete PS/2"},
        {2,VRD,"M boton medio ","0=suelto","1=presionado"},
        {1,VRD,"R boton derech","0=suelto","1=presionado"},
        {0,VRD,"L boton izqui.","0=suelto","1=presionado"},
    };
    for(int i=0;i<8;i++){
        bool on=(e.reg_estado_mouse>>bits[i].bit)&1;
        std::string line=std::string("  bit")+std::to_string(bits[i].bit)+" "+bits[i].nom+": ";
        bstr(2,19+i,GRI,line);
        bstr(40,19+i,on?bits[i].con:GRI,on?"1":"0");
        bstr(42,19+i,on?bits[i].con:GRI,std::string(" = ")+(on?bits[i].d1:bits[i].d0));
    }
    // Botones (fila compacta al final)
    bstr(2,BUF_H-9,GRI,"Botones:  ");
    bstr(12,BUF_H-9,e.btn_izq?VRD:GRI,e.btn_izq?"[IZQ]":" izq ");
    bstr(18,BUF_H-9,e.btn_mid?VRD:GRI,e.btn_mid?"[MID]":" mid ");
    bstr(24,BUF_H-9,e.btn_der?VRD:GRI,e.btn_der?"[DER]":" der ");
    bstr(30,BUF_H-9,e.btn_x1?MGA:GRI, e.btn_x1?"[X1] ":" x1  ");
    bstr(36,BUF_H-9,e.btn_x2?MGA:GRI, e.btn_x2?"[X2] ":" x2  ");
    bstr(42,BUF_H-9,GRI,"<- X1/X2=botones laterales; Byte4[5:4]");
    bstr(2,BUF_H-8,GRI,"IRQ12 Raton PS/2:"); bstr(20,BUF_H-8,MGA,fw(e.mouse_eventos,8));
    bstr(29,BUF_H-8,GRI,"<- IRQ12 -> vector 0x74 -> ISR lee 4 bytes (modo IntelliMouse)");
}

// ---- PANEL 2: TECLADO ----
void render_teclado(const Estado& e){
    bbox(0,4,BUF_W,BUF_H-8,AMR,"TECLADO - Controlador i8042 | F1-F6=panel directo");

    // --- Nombre VK ---
    std::string vk_nombre;
    switch(e.ultimo_vk){
        case VK_LSHIFT:   vk_nombre="VK_LSHIFT   Shift izquierdo"; break;
        case VK_RSHIFT:   vk_nombre="VK_RSHIFT   Shift derecho";   break;
        case VK_LCONTROL: vk_nombre="VK_LCONTROL Ctrl izquierdo";  break;
        case VK_RCONTROL: vk_nombre="VK_RCONTROL Ctrl derecho";    break;
        case VK_LMENU:    vk_nombre="VK_LMENU    Alt izquierdo";   break;
        case VK_RMENU:    vk_nombre="VK_RMENU    AltGr (LAm)";     break;
        case VK_CAPITAL:  vk_nombre="VK_CAPITAL  Bloq Mayus";      break;
        case VK_NUMLOCK:  vk_nombre="VK_NUMLOCK  Bloq Num";        break;
        case VK_SCROLL:   vk_nombre="VK_SCROLL   Bloq Despl";      break;
        case VK_RETURN:   vk_nombre="VK_RETURN   Enter (0x0D)";    break;
        case VK_SPACE:    vk_nombre="VK_SPACE    Espacio (0x20)";  break;
        case VK_BACK:     vk_nombre="VK_BACK     Retroceso";       break;
        case VK_TAB:      vk_nombre="VK_TAB      Tabulador";       break;
        case VK_ESCAPE:   vk_nombre="VK_ESCAPE   Esc";             break;
        case VK_DELETE:   vk_nombre="VK_DELETE   Supr";            break;
        case VK_INSERT:   vk_nombre="VK_INSERT   Ins";             break;
        case VK_HOME:     vk_nombre="VK_HOME     Inicio";          break;
        case VK_END:      vk_nombre="VK_END      Fin";             break;
        case VK_PRIOR:    vk_nombre="VK_PRIOR    RePag";           break;
        case VK_NEXT:     vk_nombre="VK_NEXT     AvPag";           break;
        case VK_PAUSE:    vk_nombre="VK_PAUSE    Pausa/Inter";     break;
        case VK_SNAPSHOT: vk_nombre="VK_SNAPSHOT ImprPant/SysRq"; break;
        case VK_LWIN:     vk_nombre="VK_LWIN     Win izquierdo";  break;
        case VK_RWIN:     vk_nombre="VK_RWIN     Win derecho";    break;
        case VK_APPS:     vk_nombre="VK_APPS     Menu Contexto";  break;
        case VK_UP:       vk_nombre="VK_UP       Flecha arriba";  break;
        case VK_DOWN:     vk_nombre="VK_DOWN     Flecha abajo";   break;
        case VK_LEFT:     vk_nombre="VK_LEFT     Flecha izq";     break;
        case VK_RIGHT:    vk_nombre="VK_RIGHT    Flecha der";     break;
        default:
            if(e.ultimo_vk>=VK_F1&&e.ultimo_vk<=VK_F12){
                int fn=e.ultimo_vk-VK_F1+1;
                vk_nombre="VK_F"+std::to_string(fn)+(fn<10?"          ":"         ")+"Funcion F"+std::to_string(fn);
            } else if(e.ultimo_vk>=0x41&&e.ultimo_vk<=0x5A){
                char c=(char)e.ultimo_vk;
                vk_nombre=std::string("VK_")+c+"          Letra "+c;
            } else if(e.ultimo_vk>=VK_NUMPAD0&&e.ultimo_vk<=VK_NUMPAD9){
                vk_nombre="VK_NUMPAD"+std::to_string(e.ultimo_vk-VK_NUMPAD0)+"     Teclado numerico";
            } else if(e.ultimo_vk==VK_MULTIPLY) vk_nombre="VK_MULTIPLY  Numpad *";
            else if(e.ultimo_vk==VK_ADD)        vk_nombre="VK_ADD       Numpad +";
            else if(e.ultimo_vk==VK_SUBTRACT)   vk_nombre="VK_SUBTRACT  Numpad -";
            else if(e.ultimo_vk==VK_DIVIDE)     vk_nombre="VK_DIVIDE    Numpad /";
            else if(e.ultimo_vk==VK_DECIMAL)    vk_nombre="VK_DECIMAL   Numpad .";
            else { vk_nombre="VK="+std::to_string(e.ultimo_vk)+" (ver MSDN)"; }
    }

    // --- Seccion 1: Ultima tecla ---
    bstr(2,5,AMR,"[Ultima tecla - IRQ1 Teclado i8042 -> vector 0x09 -> ISR]");
    {
        std::string disp;
        if(e.ultimo_char>31 && e.ultimo_char<127)
            disp=std::string(1,e.ultimo_char)+" (ASCII "+std::to_string((int)e.ultimo_char)+")";
        else if(e.ultimo_char=='~')
            disp="~ (extendido latinoam.: acento/enie/simbolo)";
        else if(e.ultimo_vk==VK_LSHIFT||e.ultimo_vk==VK_RSHIFT)
            disp="[SHIFT]  <- modificadora";
        else if(e.ultimo_vk==VK_CAPITAL)  disp="[CAPS]   <- toggle";
        else if(e.ultimo_vk==VK_NUMLOCK)  disp="[NUM]    <- toggle";
        else if(e.ultimo_vk==VK_LCONTROL||e.ultimo_vk==VK_RCONTROL)
            disp="[CTRL]   <- modificadora";
        else if(e.ultimo_vk==VK_LMENU||e.ultimo_vk==VK_RMENU)
            disp="[ALT/AltGr] <- modificadora";
        else if(e.ultimo_vk>=VK_F1&&e.ultimo_vk<=VK_F12)
            disp="[F"+std::to_string(e.ultimo_vk-VK_F1+1)+"] tecla de funcion (sin caracter)";
        else if(e.ultimo_vk==VK_INSERT)  disp="[INS]  Ins="+std::string(e.ins_activo?"ACTIVO":"inactivo");
        else if(e.ultimo_vk==VK_DELETE)  disp="[SUPR] Borrar caracter";
        else if(e.ultimo_vk==VK_HOME)    disp="[INICIO] Ir al principio";
        else if(e.ultimo_vk==VK_END)     disp="[FIN] Ir al final";
        else if(e.ultimo_vk==VK_PRIOR)   disp="[REPAG] Pagina anterior";
        else if(e.ultimo_vk==VK_NEXT)    disp="[AVPAG] Pagina siguiente";
        else if(e.ultimo_vk==VK_PAUSE)   disp="[PAUSA] Pausa/Interrumpir";
        else if(e.ultimo_vk==VK_SNAPSHOT)disp="[IMPANT] ImprPant/SysRq";
        else if(e.ultimo_vk==VK_LWIN||e.ultimo_vk==VK_RWIN) disp="[WIN] Tecla Windows";
        else if(e.ultimo_vk==VK_APPS)    disp="[MENU] Menu contexto";
        else disp="(sin caracter imprimible)";
        bstr(2,6,GRI,"Caracter     :"); bstr(17,6,VRD,disp);
    }
    bstr(2,7,GRI,"Make Code    :"); bstr(17,7,CYN,fhex(e.ultimo_scancode,2));
    bstr(22,7,GRI,"<- codigo fisico tecla (puerto 0x60)");
    {   BYTE bc=e.ultimo_scancode|0x80;
        bstr(2,8,GRI,"Break Code   :"); bstr(17,8,GRI,fhex(bc,2));
        bstr(22,8,GRI,"<- Make|0x80 (bit7=1 = tecla soltada)"); }
    bstr(2,9,GRI,"Virtual Key  :"); bstr(17,9,CYN,fhex(e.ultimo_vk,4));
    bstr(22,9,GRI,vk_nombre);

    // --- Seccion 2: Registro i8042 ---
    bhline(1,10,BUF_W-2,'-',GRI);
    bstr(2,11,AMR,"[Registro Estado 0x64 i8042]");
    bstr(2,12,GRI,"Puerto 0x64:"); bstr(14,12,CYN,fhex(e.reg_estado_kbd,2));
    struct { int bit; WORD con; const char* nom; const char* d0; const char* d1; } bits[]={
        {0,VRD,"OBF","buf vacio","scancode listo en 0x60"},
        {1,AMR,"IBF","CPU puede enviar","i8042 ocupado"},
        {2,BLC,"SYS","POST en progreso","POST OK"},
        {3,GRI,"CMD","ultimo=dato","ultimo=comando"},
        {4,VRD,"INH","kbd DESHABILITADO","kbd HABILITADO"},
        {5,MGA,"MOBF","dato=teclado","dato=raton"},
        {6,ROJ,"TO ","sin timeout","error timeout"},
        {7,ROJ,"PE ","sin paridad","error paridad"},
    };
    for(int i=0;i<8;i++){
        bool on=(e.reg_estado_kbd>>bits[i].bit)&1;
        bstr(2+i*10,13,GRI,std::string("b")+std::to_string(bits[i].bit)+":"+bits[i].nom);
        bstr(2+i*10,14,on?bits[i].con:GRI,on?"1 ":"0 ");
        // descripcion corta debajo
    }
    bstr(2,15,GRI,"Puerto 0x60  :"); bstr(17,15,CYN,fhex(e.reg_datos_kbd,2));
    bstr(22,15,GRI,"<- scancode Make del i8042");

    // --- Seccion 3: Teclas Fn ---
    bhline(1,16,BUF_W-2,'-',GRI);
    bstr(2,17,AMR,"[Teclas de Funcion F1-F12 | F1-F6 = salto directo de panel]");
    {
        // Fila F1-F12 visual
        const char* fn_labels[]={"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
        // scancodes Set 1 de F1-F12
        const BYTE fn_sc[]={0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x41,0x42,0x43,0x44,0x57,0x58};
        for(int i=0;i<12;i++){
            bool activa=(e.ultimo_vk==(WORD)(VK_F1+i));
            bool esFn16=i<6;
            WORD col = activa ? ROJ : (esFn16 ? AMR : CYN);
            std::string lbl="["+std::string(fn_labels[i])+(i<9?" ]":"]");
            bstr(2+i*6,18,col,lbl);
            bstr(2+i*6,19,GRI,fhex(fn_sc[i],2));
        }
        bstr(2,20,GRI,"Sc:");
        bstr(5,20,GRI,"F1-F4=3B-3E  F5-F8=3F-42  F9-F12=43,44,57,58");
        bstr(2,21,GRI,"F1-F8=panel directo  F7=Sistema/Drivers  F8=Disco Detalle");
    }

    // --- Seccion 4: Modificadoras ---
    bhline(1,22,BUF_W-2,'-',GRI);
    bstr(2,23,AMR,"[Modificadoras + Teclas Sistema]");
    bstr(2,24,GRI,"CAPS:"); bstr(8,24,e.caps?VRD:GRI,e.caps?"ON":"--");
    bstr(13,24,GRI,"NUM:"); bstr(18,24,e.num?VRD:GRI,e.num?"ON":"--");
    bstr(23,24,GRI,"SCR:"); bstr(28,24,e.scroll?VRD:GRI,e.scroll?"ON":"--");
    bstr(33,24,GRI,"SHIFT:"); bstr(40,24,e.shift?AMR:GRI,e.shift?"ON":"--");
    bstr(44,24,GRI,"CTRL:"); bstr(50,24,e.ctrl?ROJ:GRI,e.ctrl?"ON":"--");
    bstr(54,24,GRI,"ALT:"); bstr(59,24,e.alt?ROJ:GRI,e.alt?"ON":"--");
    bstr(63,24,GRI,"AltGr:"); bstr(70,24,(e.ctrl&&e.alt)?MGA:GRI,(e.ctrl&&e.alt)?"ON":"--");

    // --- Seccion 5: Teclas especiales E0 ---
    bhline(1,25,BUF_W-2,'-',GRI);
    bstr(2,26,AMR,"[Teclas Especiales - Scancodes extendidos E0]");
    auto showTK=[&](int x,int y,WORD vkk,const char* label){
        bool last=(e.ultima_tk_especial==vkk);
        bstr(x,y,last?ROJ:GRI,label);
    };
    bstr(2,27,GRI,"Nav:");
    showTK( 7,27,VK_INSERT,  "[INS]"); bstr(12,27,e.ins_activo?CYN:GRI,e.ins_activo?"*":".");
    showTK(14,27,VK_DELETE,  "[SUP]");
    showTK(20,27,VK_HOME,    "[INI]");
    showTK(26,27,VK_END,     "[FIN]");
    showTK(32,27,VK_PRIOR,   "[RPG]");
    showTK(38,27,VK_NEXT,    "[APG]");
    bstr(44,27,GRI,"  Sist:");
    showTK(52,27,VK_PAUSE,    "[PAU]");
    showTK(58,27,VK_SNAPSHOT, "[IMP]");
    showTK(64,27,VK_LWIN,     "[WL]");
    showTK(68,27,VK_RWIN,     "[WR]");
    showTK(72,27,VK_APPS,     "[MNU]");
    bstr(2,28,GRI,"IRQ1 KBD:"); bstr(12,28,MGA,fw(e.kbd_eventos,6));
    bstr(19,28,GRI,"  <- vec 0x09  Make=presion Break=soltar(Make|0x80)");
}

// ---- PANEL 3: DISCO + DMA ----
void render_disco(const Estado& e){
    bbox(0,4,BUF_W,BUF_H-8,VRD,"DISCO + DMA 8237A  [E/S de TODO el sistema via PDH]");

    bstr(2,5,AMR,"[Velocidades SISTEMA COMPLETO - PhysicalDisk(_Total) - incluye Explorer]");
    {   WORD col=e.vel_lect_mb>200?ROJ:e.vel_lect_mb>50?AMR:VRD;
        bstr(2,6,GRI,"Lect.Sistema :"); bstr(17,6,col,fw((int)e.vel_lect_mb,6)); bstr(24,6,GRI,"MB/s");
        bstr(30,6,col,barra(std::min(e.vel_lect_mb,500.0)/5.0,30));
        bstr(62,6,GRI,"ops/s:"); bstr(69,6,col,fw((int)e.sys_disk_rops,5)); }
    {   WORD col=e.vel_escr_mb>200?ROJ:e.vel_escr_mb>50?AMR:VRD;
        bstr(2,7,GRI,"Escr.Sistema :"); bstr(17,7,col,fw((int)e.vel_escr_mb,6)); bstr(24,7,GRI,"MB/s");
        bstr(30,7,col,barra(std::min(e.vel_escr_mb,500.0)/5.0,30));
        bstr(62,7,GRI,"ops/s:"); bstr(69,7,col,fw((int)e.sys_disk_wops,5)); }
    bstr(2,8,GRI,"CPU sistema  :");
    {   WORD col=e.sys_cpu_pct>80?ROJ:e.sys_cpu_pct>50?AMR:VRD;
        bstr(17,8,col,fpct(e.sys_cpu_pct));
        bstr(24,8,col,barra(e.sys_cpu_pct,30)); }
    bstr(2,9,GRI,"Total leido  :"); bstr(17,9,CYN,fmb(e.bytes_leidos));
    bstr(32,9,GRI,"Total escrito:"); bstr(47,9,CYN,fmb(e.bytes_escritos));

    bhline(1,9,BUF_W-2,'-',GRI);
    bstr(2,10,AMR,"[Registros del controlador DMA 8237A - canal 2]");
    bstr(2,11,GRI,"Dir.Base(0x04):"); bstr(18,11,CYN,fhex(e.dma_dir_base));
    bstr(28,11,GRI,"dir.RAM destino; DMA escribe datos del disco aqui");
    bstr(2,12,GRI,"Contador(0x05):"); bstr(18,12,(e.dma_contador?AMR:GRI),fhex(e.dma_contador));
    bstr(28,12,GRI,"bytes restantes; se decrementa solo cada ciclo DMA");
    bstr(2,13,GRI,"Modo    (0x0B):"); bstr(18,13,BLC,fhex(e.dma_modo,2));
    bstr(22,13,GRI,"= 0x58 bits:");
    bstr(34,13,CYN,"[7:6]=01"); bstr(43,13,GRI,"BLOQUE  |");
    bstr(53,13,CYN,"[5:4]=01"); bstr(62,13,GRI,"LECTURA");
    bstr(22,14,GRI,"            ");
    bstr(34,14,CYN,"[3:2]=10"); bstr(43,14,GRI,"CANAL2  |");
    bstr(53,14,CYN,"[1:0]=00"); bstr(62,14,GRI,"AUTO-INIT");
    bstr(2,15,GRI,"Estado  (0x08):"); bstr(18,15,(e.dma_estado==0x04?AMR:VRD),fhex(e.dma_estado,2));
    {   std::string d=(e.dma_estado==0x04)?
            "0x04=bit2 activo: DMA en curso (cycle-steal del bus)":
            "0x0F=bits 0-3 en 1: Terminal Count, transfencia OK  ";
        bstr(22,15,e.dma_estado==0x04?AMR:GRI,d); }
    bstr(2,16,GRI,"Mascara (0x0A):"); bstr(18,16,GRI,"0x00");
    bstr(22,16,GRI,"bit=0 por canal = canal HABILITADO; bit=1 = canal enmascarado");

    bhline(1,17,BUF_W-2,'-',GRI);
    bstr(2,18,AMR,"[Ciclo DMA completo - Tanenbaum Fig.5-4 - CPU libre en pasos 2-5]");
    bstr(2,19,GRI,"P1 CPU    : Escribe Dir.Base+Contador+Modo; envia READ al disco");
    bstr(2,20,GRI,"P2 DMA    : Solicita bus (HOLD); CPU responde HLDA y suelta el bus");
    bstr(2,21,GRI,"P3 Disco  : DMA pone Dir.Base en bus dir.; dato del disco en bus dat.");
    bstr(2,22,GRI,"P4 DMA    : Contador--; Dir.Base++; si Contador>0 vuelve a P3");
    bstr(2,23,GRI,"P5 DMA    : Contador=0 -> TC=1 -> libera bus -> genera IRQ14");
    bstr(2,24,GRI,"P6 IRQ14  : Vector 0x76 -> ISR: verifica sector, desbloquea proceso");
    bstr(2,25,GRI,"IRQ14 IDE1:"); bstr(14,25,ROJ,fw(e.irq14_count,6));
    bstr(21,25,GRI,"interrupciones IDE  |  PDH=Performance Data Helper");

    bhline(1,26,BUF_W-2,'-',GRI);
    bstr(2,27,AMR,"[Actividad de archivos - ReadDirectoryChangesW - %USERPROFILE%]");
    bstr(2,28,GRI,"Tipo   Archivo/Carpeta (monitoreo recursivo de subdirectorios)");
    EnterCriticalSection(&g_cs);
    int start=e.n_eventos>=2?e.n_eventos-2:0;
    for(int i=start;i<e.n_eventos&&i<start+2;i++){
        int fy=29+(i-start);
        DWORD age=(GetTickCount()-e.eventos_arch[i].tick)/1000;
        WORD col=(age<3)?AMR:(age<10)?CYN:GRI;
        std::string nm=e.eventos_arch[i].nombre;
        if((int)nm.size()>70)nm=nm.substr(0,67)+"...";
        bstr(2,fy,col,std::string(e.eventos_arch[i].tipo)+" "+nm);
        std::ostringstream ag; ag<<age<<"s";
        bstr(95,fy,GRI,ag.str());
    }
    if(e.n_eventos==0) bstr(2,29,GRI,"(esperando... mueve o crea un archivo en tu carpeta de usuario)");
    LeaveCriticalSection(&g_cs);
}

// ---- PANEL 4: CPU + TIMER ----
void render_cpu(const Estado& e){
    bbox(0,4,BUF_W,BUF_H-8,MGA,"CPU + TIMER PIT 8253/8254");

    bstr(2,5,AMR,"[Uso de CPU del proceso]");
    {   WORD c=e.cpu_pct>80?ROJ:e.cpu_pct>50?AMR:VRD;
        bstr(2,6,GRI,"Uso CPU      :"); bstr(17,6,c,fpct(e.cpu_pct));
        bstr(26,6,c,barra(e.cpu_pct,28)); }

    bhline(1,7,BUF_W-2,'-',GRI);
    bstr(2,8,AMR,"[Registros del PIT 8253/8254 - Tanenbaum 5.5.1 - puertos 0x40-0x43]");
    bstr(2,9,GRI, "Puerto 0x40  :"); bstr(17,9,CYN,fhex(e.pit_counter0,4));
    bstr(24,9,GRI,"Counter0 - genera IRQ0; cuenta 11932->0 a 1.19MHz -> ~100Hz");
    bstr(2,10,GRI,"Puerto 0x41  :"); bstr(17,10,GRI,"N/A ");
    bstr(24,10,GRI,"Counter1 - DRAM refresh (obsoleto en PCs modernas post-286)");
    bstr(2,11,GRI,"Puerto 0x42  :"); bstr(17,11,GRI,"0x00");
    bstr(24,11,GRI,"Counter2 - altavoz PC; frecuencia = 1193182 / divisor");
    bstr(2,12,GRI,"Puerto 0x43  :"); bstr(17,12,BLC,"0x36");
    bstr(24,12,GRI,"Control Word: configura modo de operacion de Counter0");

    bhline(1,13,BUF_W-2,'-',GRI);
    bstr(2,14,AMR,"[Decodificacion Control Word 0x43 = 0x36 = 0011 0110 binario]");
    bstr(2,15,GRI,"  bits[7:6] = 00 ->"); bstr(22,15,CYN,"Canal 0         ");
    bstr(38,15,GRI,"(Counter0 = IRQ0, timer del scheduler)");
    bstr(2,16,GRI,"  bits[5:4] = 11 ->"); bstr(22,16,CYN,"LSB+MSB         ");
    bstr(38,16,GRI,"(primero 8 bits bajos, luego 8 bits altos)");
    bstr(2,17,GRI,"  bits[3:1] = 011->"); bstr(22,17,CYN,"Modo 3 onda cuad");
    bstr(38,17,GRI,"(se recarga auto -> IRQ0 continua a freq. fija)");
    bstr(2,18,GRI,"  bit [0]   = 0  ->"); bstr(22,18,CYN,"Binario         ");
    bstr(38,18,GRI,"(conteo binario; si fuera 1 seria BCD)");

    bhline(1,19,BUF_W-2,'-',GRI);
    bstr(2,20,AMR,"[Tareas del SO en cada tick IRQ0 - Tanenbaum 5.5.2]");
    bstr(2,21,GRI,"1. Incrementa GetTickCount() y SYSTEMTIME del sistema");
    bstr(2,22,GRI,"2. Decrementa quantum del proceso; quantum=0 -> scheduler");
    bstr(2,23,GRI,"3. Decrementa temporizadores (Sleep, WaitForSingleObject)");
    bstr(2,24,GRI,"4. Contabiliza tiempo de CPU del proceso actual");
    bstr(2,25,GRI,"IRQ0 ticks   :"); bstr(17,25,CYN,fw(e.irq0_count,10)); bstr(28,25,GRI,"ms desde boot");
    {   DWORD seg=e.uptime_ms/1000;
        std::ostringstream up; up<<seg/3600<<"h "<<(seg%3600)/60<<"m "<<seg%60<<"s";
        bstr(2,26,GRI,"Uptime       :"); bstr(17,26,VRD,up.str()); }
}

// ---- PANEL 5: MEMORIA ----
void render_memoria(const Estado& e){
    bbox(0,4,BUF_W,BUF_H-8,AZL,"MEMORIA + Page Faults + MMU");

    bstr(2,5,AMR,"[RAM fisica del sistema - Tanenbaum 5.1.3]");
    bstr(2,6,GRI,"RAM Total    :"); bstr(17,6,GRI,fmb(e.ram_total));
    bstr(28,6,GRI,barra((double)(e.ram_total-e.ram_libre)*100.0/e.ram_total,26));
    bstr(2,7,GRI,"RAM Libre    :"); bstr(17,7,VRD,fmb(e.ram_libre));
    bstr(28,7,GRI,"paginas fisicas disponibles sin necesidad de swap");
    bstr(2,8,GRI,"RAM Usada    :");
    {   WORD c=e.pct_ram>85?ROJ:e.pct_ram>65?AMR:VRD;
        bstr(17,8,c,fpct(e.pct_ram));
        bstr(26,8,GRI,">85% -> SO pagina agresivamente (thrashing)"); }

    bhline(1,9,BUF_W-2,'-',GRI);
    bstr(2,10,AMR,"[Working Set del proceso - Tanenbaum 3.5.1]");
    bstr(2,11,GRI,"Working Set  :"); bstr(17,11,CYN,fmb(e.ws_actual));
    bstr(28,11,GRI,"paginas fisicas activas de este proceso ahora mismo");
    bstr(2,12,GRI,"Peak WS      :"); bstr(17,12,GRI,fmb(e.ws_peak));
    bstr(28,12,GRI,"maximo WS alcanzado (util para dimensionar RAM necesaria)");

    bhline(1,13,BUF_W-2,'-',GRI);
    bstr(2,14,AMR,"[Page Faults = activaciones DMA de paginacion - Tanenbaum 5.2.4]");
    bstr(2,15,GRI,"PF acumulados:"); bstr(17,15,ROJ,fw(e.pf_total,10));
    bstr(28,15,GRI,"total desde inicio; cada PF = pagina no estaba en RAM");
    bstr(2,16,GRI,"PF este frame:"); bstr(17,16,(e.pf_delta>0?AMR:GRI),fw(e.pf_delta,10));
    bstr(28,16,GRI,"nuevos PF en los ultimos 200ms");
    bstr(2,17,GRI,"DMA PF/s     :"); bstr(17,17,(e.dma_pf_activos>100?ROJ:AMR),fw(e.dma_pf_activos,10));
    bstr(28,17,GRI,"tasa PF/s; cada PF puede implicar 1 transferencia DMA");
    bstr(2,18,GRI,"Page File uso:"); bstr(17,18,GRI,fmb(e.pf_sys_total));
    bstr(28,18,GRI,"pagefile.sys = extension de RAM en disco (Tanenbaum 3.7)");

    bhline(1,19,BUF_W-2,'-',GRI);
    bstr(2,20,AMR,"[Ciclo completo de Page Fault - Tanenbaum 3.4 + 5.1.3 + 5.2.4]");
    bstr(2,21,GRI,"P1: Proceso accede dir. virtual V -> MMU: bit_P=0 -> Page Fault");
    bstr(2,22,GRI,"P2: ISR del PF busca pagina en pagefile.sys en disco");
    bstr(2,23,GRI,"P3: DMA copia pagina (4KB) disco->RAM; CPU totalmente libre");
    bstr(2,24,GRI,"P4: MMU actualiza tabla de paginas: bit_P=1, dir. fisica real");
    bstr(2,25,GRI,"P5: TLB invalida entrada vieja; proceso se reanuda transparente");
    bstr(2,26,GRI,"Barra PF/s   :"); bstr(17,26,ROJ,barra(std::min((double)e.dma_pf_activos,200.0)/2.0,35));
}

// ---- PANEL 6: CANALES DMA ----
void render_dma(const Estado& e){
    bbox(0,4,BUF_W,BUF_H-8,CYN,"CANALES DMA 8237A - 8 canales (Tanenbaum 5.1.4)");

    bstr(2,5,AMR,"[Tabla de canales - puertos 0x00-0x0F (maestro) / 0xC0-0xDF (esclavo)]");
    bstr(2,6,GRI,"CH Dispositivo  Dir.Base   Contad. Modo Estado");
    bhline(1,7,BUF_W-2,'-',GRI);

    for(int i=0;i<8;i++){
        const Estado::Canal& c=e.canales[i];
        int fy=8+i;
        bstr(2,fy,GRI,std::to_string(i));
        bstr(4,fy,c.activo?AMR:(i==4?CYN:GRI),std::string(c.dispositivo).substr(0,9));
        bstr(14,fy,c.activo?VRD:GRI,fhex(c.dir_base,6));
        bstr(23,fy,c.activo?AMR:GRI,fw(c.contador,7));
        bstr(31,fy,GRI,fhex(c.modo,2));
        if(i==4)                   bstr(35,fy,CYN,"CASCADE maestro(conecta DMA A y B)");
        else if(c.activo)          bstr(35,fy,VRD,"ACTIVO - cycle-steal en curso      ");
        else if(c.terminal_count)  bstr(35,fy,GRI,"TC=1   - transferencia completada  ");
        else                       bstr(35,fy,GRI,"libre  - canal disponible          ");
    }

    bhline(1,17,BUF_W-2,'-',GRI);
    bstr(2,18,AMR,"[Descripcion de canales segun uso estandar IBM PC]");
    bstr(2,19,GRI,"CH0: Mem->Mem   refresh DRAM (obsoleto en PCs con DRAM sincronizada)");
    bstr(2,20,GRI,"CH1: Audio/ISA  tarjetas de sonido ISA legacy (Sound Blaster)");
    bstr(2,21,GRI,"CH2: Disquete   FDC; en discos NVMe/SATA lo maneja el driver PCIe");
    bstr(2,22,GRI,"CH3: LPT1       puerto paralelo (impresora ISA)");
    bstr(2,23,GRI,"CH4: CASCADE    conecta chip esclavo(0-3) al maestro(4-7) via bus");
    bstr(2,24,GRI,"CH5-7: ISA 16b  canales libres para expansion de hardware ISA");
    bstr(2,25,GRI,"DMA dir.base:"); bstr(16,25,VRD,fhex(e.dma_dir_base));
    bstr(27,25,GRI,"Modo 0x58 ="); bstr(39,25,BLC,"bloque+lectura+auto-init+canal2");
    bstr(2,26,GRI,"IRQ14 IDE1  :"); bstr(16,26,ROJ,fw(e.irq14_count,6));
    bstr(23,26,GRI,"<- vector 0x76, Terminal Count canal2, IDE primario");
}

// ---- PANEL 7: SISTEMA - VENTANAS, PROCESOS, DRIVERS ----
void render_sistema(const Estado& e){
    bbox(0,4,BUF_W,BUF_H-8,MGA,"SISTEMA - Ventanas / Procesos / Drivers del Kernel");

    // -- Ventanas y procesos --
    bstr(2,5,AMR,"[Gestor de Ventanas - USER32 / win32k.sys - Tanenbaum 11.3]");
    bstr(2,6,GRI,"Ventanas totales :"); bstr(21,6,CYN,fw(e.ventanas_totales,6));
    bstr(28,6,GRI,"<- EnumWindows() incluye ventanas ocultas y de sistema");
    bstr(2,7,GRI,"Ventanas visibles:"); bstr(21,7,VRD,fw(e.ventanas_visibles,6));
    bstr(28,7,GRI,"<- IsWindowVisible()=TRUE; las que ves en pantalla ahora");
    bstr(2,8,GRI,"Ventanas ocultas :"); bstr(21,8,GRI,fw(e.ventanas_totales-e.ventanas_visibles,6));
    bstr(28,8,GRI,"<- servicios, shell, bandeja, mensajes de fondo");

    bhline(1,9,BUF_W-2,'-',GRI);
    bstr(2,10,AMR,"[Procesos e Hilos - Scheduler del kernel - Tanenbaum 2.1 / 2.4]");
    bstr(2,11,GRI,"Procesos totales :"); bstr(21,11,AMR,fw(e.procesos_total,6));
    bstr(28,11,GRI,"<- cada proceso = espacio de direcciones + recursos propios");
    bstr(2,12,GRI,"Hilos totales    :"); bstr(21,12,AMR,fw(e.hilos_total,6));
    bstr(28,12,GRI,"<- unidad de planificacion; scheduler elige 1 hilo por tick");
    bstr(2,13,GRI,"Hilos/Proceso    :"); 
    {
        double ratio = e.procesos_total>0 ? (double)e.hilos_total/e.procesos_total : 0;
        std::ostringstream r; r<<std::fixed<<std::setprecision(1)<<ratio;
        bstr(21,13,CYN,r.str());
    }
    bstr(28,13,GRI,"<- promedio de hilos por proceso en el sistema");
    bstr(2,14,GRI,"Handles proceso  :"); bstr(21,14,GRI,fw(e.handles_proceso,6));
    bstr(28,14,GRI,"<- archivos, eventos, secciones abiertos por este proceso");
    bstr(2,15,GRI,"Mem. Comprometida :"); bstr(21,15,GRI,fmb(e.bytes_commit));
    bstr(28,15,GRI,"<- RAM+pagefile reservado; si > RAM fisica -> swap activo");

    bhline(1,16,BUF_W-2,'-',GRI);
    bstr(2,17,AMR,"[Modulos del Kernel cargados - ntoskrnl + drivers - Tanenbaum 11.4]");
    bstr(2,18,GRI,"Nombre driver       Base addr   Descripcion");
    bhline(1,19,BUF_W-2,'-',GRI);

    // Tabla de drivers con descripciones conocidas
    struct { const char* nombre; const char* desc; } conocidos[]={
        {"ntoskrnl.exe","Kernel NT + scheduler + MM + I/O manager"},
        {"hal.dll",     "HAL: abstrae hardware (IRQ,DMA,timer) del kernel"},
        {"win32k.sys",  "Subsistema grafico Win32 (GDI+USER en kernel)"},
        {"ndis.sys",    "NDIS: capa de red (entre driver NIC y TCP/IP)"},
        {"tcpip.sys",   "Pila TCP/IP del kernel"},
        {"dxgkrnl.sys", "DirectX Graphics Kernel (GPU scheduling)"},
        {"storport.sys","Miniport almacenamiento (SATA/NVMe/SAS)"},
        {"nvlddmkm.sys","Driver display NVIDIA (modo kernel)"},
        {"ataport.sys", "Canal ATA/IDE, gestiona IRQ14/IRQ15"},
        {"fltmgr.sys",  "Filter Manager: AV y cifrado de archivos"},
        {"Wdf01000.sys","Windows Driver Framework v1"},
        {"acpi.sys",    "ACPI: gestion energia, IRQs, PnP"},
        {NULL,NULL}
    };

    for(int i=0;i<e.n_drivers&&i<10;i++){
        int fy=20+i;
        // buscar descripcion conocida
        const char* desc="(driver de sistema)";
        for(int k=0;conocidos[k].nombre;k++){
            // comparacion case-insensitive parcial
            std::string nm=e.drivers[i].nombre;
            std::string kn=conocidos[k].nombre;
            // lowercase ambos
            for(auto& ch:nm) ch=tolower(ch);
            for(auto& ch:kn) ch=tolower(ch);
            if(nm==kn){ desc=conocidos[k].desc; break; }
        }
        bstr(2,fy,i<3?AMR:GRI,e.drivers[i].nombre);
        bstr(22,fy,CYN,e.drivers[i].base);
        bstr(35,fy,GRI,desc);
    }
}

// ---- PANEL 8: DISCO DETALLADO ----
void render_disco_detalle(const Estado& e){
    bbox(0,4,BUF_W,BUF_H-8,VRD,"DISCO DURO - Mapa Completo + Geometria + E/S (Tanenbaum 5.4)");

    // -- Seccion 1: Identificacion del volumen --
    bstr(2,5,AMR,"[Identificacion del Volumen - Tabla de particiones / MBR]");
    bstr(2,6,GRI,"Unidad       : "); bstr(17,6,CYN,"C:"); bstr(19,6,CYN,"\\");
    bstr(21,6,GRI,"Volumen: "); bstr(30,6,AMR,std::string(e.disco_volumen[0]?e.disco_volumen:"(sin etiq.)"));
    bstr(50,6,GRI,"FS: "); bstr(54,6,VRD,std::string(e.disco_fs[0]?e.disco_fs:"?"));
    bstr(62,6,GRI,"Serial: "); bstr(70,6,CYN,fhex(e.disco_serial,8));

    // -- Seccion 2: Geometria logica del disco --
    bhline(1,7,BUF_W-2,'-',GRI);
    bstr(2,8,AMR,"[Geometria Logica - LBA (Logical Block Addressing) - Tanenbaum 5.4.2]");
    bstr(2,9,GRI,"Bytes/sector  :"); bstr(18,9,CYN,fw(e.disco_bytes_sector,6));
    bstr(25,9,GRI,"<- sector = unidad minima de lectura/escritura del disco");
    bstr(2,10,GRI,"Sect/cluster  :"); bstr(18,10,CYN,fw(e.disco_sectores_cluster,6));
    bstr(25,10,GRI,"<- cluster = unidad minima del sistema de archivos (NTFS)");
    {
        DWORD tam_cluster = e.disco_sectores_cluster * e.disco_bytes_sector;
        bstr(2,11,GRI,"Bytes/cluster :"); bstr(18,11,AMR,fw(tam_cluster,6));
        bstr(25,11,GRI,"<- fragmentacion interna: archivo pequeno ocupa 1 cluster entero");
    }
    bstr(2,12,GRI,"Clusters total:"); bstr(18,12,GRI,fw(e.disco_clusters_total,10));
    bstr(29,12,GRI,"<- total de clusters direccionables por el FS");
    bstr(2,13,GRI,"Clusters libre:"); bstr(18,13,VRD,fw(e.disco_clusters_libres,10));
    bstr(29,13,GRI,"<- clusters disponibles para nuevos archivos");

    // -- Seccion 3: Capacidad y uso --
    bhline(1,14,BUF_W-2,'-',GRI);
    bstr(2,15,AMR,"[Capacidad y Uso del Disco - Tanenbaum 5.4.3]");
    {
        auto fgb=[](ULONGLONG b)->std::string{
            std::ostringstream s;
            s<<std::fixed<<std::setprecision(2)<<(double)b/(1024.0*1024.0*1024.0)<<" GB";
            return s.str();
        };
        bstr(2,16,GRI,"Total        :"); bstr(17,16,BLC,fgb(e.disco_total_bytes));
        double pct_uso=e.disco_total_bytes>0?(double)e.disco_usado_bytes*100.0/e.disco_total_bytes:0;
        bstr(30,16,GRI,barra(pct_uso,30));
        bstr(62,16,(pct_uso>85?ROJ:pct_uso>65?AMR:VRD),fpct(pct_uso));
        bstr(2,17,GRI,"Usado        :"); bstr(17,17,(pct_uso>85?ROJ:AMR),fgb(e.disco_usado_bytes));
        bstr(2,18,GRI,"Libre        :"); bstr(17,18,VRD,fgb(e.disco_libre_bytes));
        bstr(30,18,GRI,"<- menos de 10% libre = riesgo de fragmentacion severa");
    }

    // -- Seccion 4: Estructura NTFS / mapa logico --
    bhline(1,19,BUF_W-2,'-',GRI);
    bstr(2,20,AMR,"[Estructura NTFS en disco - Tanenbaum 5.4.4 + 11.8]");
    bstr(2,21,GRI,"Sector 0     :"); bstr(17,21,CYN,"VBR / BPB");
    bstr(27,21,GRI,"<- Volume Boot Record; contiene BPB con geometria del volumen");
    bstr(2,22,GRI,"Sector 1-15  :"); bstr(17,22,CYN,"IPL (bootloader)");
    bstr(35,22,GRI,"<- cargado por MBR; transfiere control al gestor de arranque");
    bstr(2,23,GRI,"$MFT         :"); bstr(17,23,AMR,"Master File Table");
    bstr(35,23,GRI,"<- 1 entrada de 1KB por archivo; contiene atributos+datos");
    bstr(2,24,GRI,"$MFTMirr     :"); bstr(17,24,AMR,"Copia MFT parcial");
    bstr(35,24,GRI,"<- backup de las primeras 4 entradas del MFT (recuperacion)");
    bstr(2,25,GRI,"$LogFile     :"); bstr(17,25,AMR,"Journal de transac.");
    bstr(35,25,GRI,"<- log de operaciones; garantiza consistencia tras apagado");
    bstr(2,26,GRI,"$Bitmap      :"); bstr(17,26,GRI,"Mapa de clusters");
    bstr(35,26,GRI,"<- 1 bit por cluster: 0=libre, 1=ocupado (como el FAT)");
    bstr(2,27,GRI,"$Boot        :"); bstr(17,27,GRI,"Copia VBR");
    bstr(35,27,GRI,"<- al final del volumen; usado por chkdsk");

    // -- Seccion 5: E/S en tiempo real --
    bhline(1,28,BUF_W-2,'-',GRI);
    bstr(2,29,AMR,"[E/S en Tiempo Real - IRQ14/DMA - Tanenbaum 5.4.1]");
    bstr(2,30,GRI,"Read ops     :"); bstr(17,30,CYN,fw(e.io_read_ops,10));
    bstr(28,30,GRI,"IOPS lectura~:"); bstr(43,30,(e.iops_lect>100?AMR:VRD),fw((long long)e.iops_lect,6));
    bstr(50,30,VRD,barra(std::min(e.iops_lect,200.0)/2.0,20));
    bstr(2,31,GRI,"Write ops    :"); bstr(17,31,CYN,fw(e.io_write_ops,10));
    bstr(28,31,GRI,"IOPS escritura:"); bstr(44,31,(e.iops_escr>100?AMR:VRD),fw((long long)e.iops_escr,6));
    bstr(51,31,AMR,barra(std::min(e.iops_escr,200.0)/2.0,20));
}

// ================================================================
//  RENDER PRINCIPAL
// ================================================================
void render(const Estado& e, int panel){
    buf_clear();
    render_cabecera(e,panel);
    switch(panel){
        case 0: render_raton(e);   break;
        case 1: render_teclado(e); break;
        case 2: render_disco(e);   break;
        case 3: render_cpu(e);     break;
        case 4: render_memoria(e); break;
        case 5: render_dma(e);          break;
        case 6: render_sistema(e);     break;
        case 7: render_disco_detalle(e); break;
    }
    render_pie(e);
    flush_buffer(); // solo escribe celdas que cambiaron -> sin parpadeo
}

// ================================================================
//  INPUT NO BLOQUEANTE
//  - Usa ReadConsoleInputW para capturar Unicode (teclado latinoamericano)
//  - Captura TODOS los KEY_EVENT (presion Y soltar) para detectar
//    modificadoras (Shift, Ctrl, Alt, CapsLock, NumLock, ScrollLock)
//    tanto al presionar como al soltar la tecla
// ================================================================
int capturar_input(Estado& e){
    DWORD n=0;
    GetNumberOfConsoleInputEvents(hIn,&n);
    if(!n) return 0;

    INPUT_RECORD ir; DWORD leidos;
    // Usar W (Unicode) en vez de A para leer bien teclado latinoamericano
    ReadConsoleInputW(hIn,&ir,1,&leidos);

    if(ir.EventType==MOUSE_EVENT){
        DWORD flags = ir.Event.MouseEvent.dwEventFlags;
        if(flags & MOUSE_WHEELED){
            // HIGH word of dwButtonState = wheel delta (signed)
            short wd = (short)HIWORD(ir.Event.MouseEvent.dwButtonState);
            e.wheel_delta = wd / WHEEL_DELTA;
            e.wheel_total += e.wheel_delta;
        } else if(flags & MOUSE_HWHEELED){
            short hwd = (short)HIWORD(ir.Event.MouseEvent.dwButtonState);
            e.hwheel_delta = hwd / WHEEL_DELTA;
            e.hwheel_total += e.hwheel_delta;
        } else {
            e.wheel_delta  = 0;
            e.hwheel_delta = 0;
        }
    }

    if(ir.EventType==KEY_EVENT){
        WORD vk  = ir.Event.KeyEvent.wVirtualKeyCode;
        bool down= ir.Event.KeyEvent.bKeyDown != 0;

        // -- Actualizar modificadoras en presion Y en soltar --
        // CapsLock y NumLock: estado de toggle (bit 0 del estado)
        // Shift/Ctrl/Alt: estado mientras se mantiene (bit 7 del estado)
        // Los leemos directamente del dwControlKeyState del evento,
        // que es el estado instantaneo mas confiable que GetKeyboardState
        DWORD ck = ir.Event.KeyEvent.dwControlKeyState;
        e.caps   = (ck & (CAPSLOCK_ON))   != 0;
        e.num    = (ck & (NUMLOCK_ON))    != 0;
        e.scroll = (ck & (SCROLLLOCK_ON)) != 0;
        e.shift  = (ck & (SHIFT_PRESSED)) != 0;
        e.ctrl   = (ck & (LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED)) != 0;
        e.alt    = (ck & (LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))   != 0;

        // Actualizar registro i8042 0x64 con estado real
        e.reg_estado_kbd = 0x14; // SYS=1(bit2), INH=1(bit4) siempre
        if(e.shift||e.ctrl||e.alt) e.reg_estado_kbd |= 0x01; // OBF

        if(down){
            // ---- 1. Registrar SIEMPRE (antes de cualquier return) ----
            e.ultimo_scancode = ir.Event.KeyEvent.wVirtualScanCode & 0xFF;
            e.ultimo_vk       = vk;
            WCHAR wc = ir.Event.KeyEvent.uChar.UnicodeChar;
            if(wc >= 32 && wc < 127)  e.ultimo_char = (char)wc;
            else if(wc >= 160)        e.ultimo_char = '~';
            else                      e.ultimo_char = 0;
            e.kbd_eventos++;

            // ---- 2. Teclas especiales y Fn ----
            e.tk_insert=(vk==VK_INSERT);
            e.tk_delete=(vk==VK_DELETE);
            e.tk_home  =(vk==VK_HOME);
            e.tk_end   =(vk==VK_END);
            e.tk_pgup  =(vk==VK_PRIOR);
            e.tk_pgdn  =(vk==VK_NEXT);
            e.tk_pause =(vk==VK_PAUSE);
            e.tk_prtscr=(vk==VK_SNAPSHOT);
            e.tk_winl  =(vk==VK_LWIN);
            e.tk_winr  =(vk==VK_RWIN);
            e.tk_apps  =(vk==VK_APPS);
            if(vk==VK_INSERT) e.ins_activo=!e.ins_activo;
            if(vk==VK_INSERT||vk==VK_DELETE||vk==VK_HOME||vk==VK_END||
               vk==VK_PRIOR||vk==VK_NEXT||vk==VK_PAUSE||vk==VK_SNAPSHOT||
               vk==VK_LWIN||vk==VK_RWIN||vk==VK_APPS||
               (vk>=VK_F1&&vk<=VK_F12))
                e.ultima_tk_especial=vk;

            // ---- 3. Navegacion paneles (DESPUES de registrar) ----
            if(vk=='Q')                           return -1;
            if(vk==VK_RETURN||vk==VK_RIGHT)        return  1;
            if(vk==VK_BACK  ||vk==VK_LEFT)         return  2;
            // F1-F6 salto directo a panel 0-5
            if(vk>=VK_F1&&vk<=VK_F8)              return 10+(vk-VK_F1);
        }
    }
    return 0;
}

// ================================================================
//  MAIN
// ================================================================
int main(){
    hCon=GetStdHandle(STD_OUTPUT_HANDLE);
    hIn =GetStdHandle(STD_INPUT_HANDLE);
    SetConsoleOutputCP(1252);
    SetConsoleCP(1252);

    // Forzar tamano de ventana a 100x35 para que quepan todos los paneles
    // Orden correcto en Windows: primero reducir ventana, luego ampliar buffer
    {
        SMALL_RECT minWin={0,0,79,29};
        SetConsoleWindowInfo(hCon,TRUE,&minWin);
    }
    COORD bufSize={BUF_W,BUF_H};
    SetConsoleScreenBufferSize(hCon,bufSize);
    SMALL_RECT wr={0,0,(SHORT)(BUF_W-1),(SHORT)(BUF_H-1)};
    SetConsoleWindowInfo(hCon,TRUE,&wr);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if(GetConsoleScreenBufferInfo(hCon,&csbi)){ CON_W=csbi.dwSize.X; CON_H=csbi.dwSize.Y; }

    SetConsoleMode(hIn,ENABLE_MOUSE_INPUT|ENABLE_WINDOW_INPUT|ENABLE_PROCESSED_INPUT|ENABLE_EXTENDED_FLAGS);
    SetConsoleTitleA("Monitor E/S | Tanenbaum Cap.5 | Carlos");

    // Inicializar buffer anterior con valores imposibles -> primer frame escribe todo
    memset(buf_actual,  0,    sizeof(buf_actual));
    memset(buf_anterior,0xFF, sizeof(buf_anterior));

    init_cpu();
    init_sysio();
    Sleep(400);  // PDH necesita 2 muestras, esperamos primer ciclo

    Estado e;
    memset(&e,0,sizeof(e));
    POINT pt; GetCursorPos(&pt);
    e.mouse_x=pt.x; e.mouse_y=pt.y;

    // Arrancar hilo monitor de archivos
    InitializeCriticalSection(&g_cs);
    g_estado_ptr = &e;
    HANDLE hHilo=CreateThread(NULL,0,hilo_archivos,NULL,0,NULL);
    if(hHilo) SetThreadPriority(hHilo,THREAD_PRIORITY_BELOW_NORMAL);

    const char* dn[]={"Mem->Mem","Audio/ISA","Disquete","LPT1","Cascade","ISA-16b","ISA-16b","ISA-16b"};
    for(int i=0;i<8;i++){
        e.canales[i].dispositivo=dn[i];
        e.canales[i].dir_base=0x00100000+(DWORD)(i*0x200000);
    }

    int panel=0;
    while(true){
        int accion=capturar_input(e);
        if(accion==-1) break;
        if(accion== 1) panel=(panel+1)%NPANELES;
        if(accion>=10&&accion<=17) panel=accion-10;
        if(accion== 2) panel=(panel+NPANELES-1)%NPANELES;
        actualizar(e);
        render(e,panel);
        Sleep(200);
    }

    // Limpiar al salir
    for(int i=0;i<BUF_W*BUF_H;i++){ buf_actual[i].ch=' '; buf_actual[i].attr=7; }
    flush_buffer();
    COORD c={0,0}; SetConsoleCursorPosition(hCon,c);
    SetConsoleTextAttribute(hCon,10);
    WriteConsoleA(hCon,"  Monitor cerrado. Hasta luego.\n",32,NULL,NULL);
    SetConsoleTextAttribute(hCon,7);
    Sleep(600);
    return 0;
}
