// Sistema de empleados (UNI) en C++ con conexion ODBC a SQL Server

#define NOMINMAX        // Evita que windows.h defina las macros min/max
#include <windows.h>    // Requerido por ODBC en Windows
#include <sql.h>        // Tipos y funciones base de ODBC
#include <sqlext.h>     // Extensiones ODBC (SQL_NTS, etc.)
#pragma comment(lib, "odbc32.lib")

#include <algorithm>    
#include <cctype>       
#include <iomanip>      
#include <iostream>     
#include <limits>       
#include <memory>       
#include <string>
#include <vector>

using namespace std;

// ==========================================
// Funciones auxiliares
// ==========================================

// Convierte un texto a mayusculas, usado para calcular el RFC
string aMayusculas(string texto) {
    transform(texto.begin(), texto.end(), texto.begin(),
              [](unsigned char c) { return static_cast<char>(toupper(c)); });
    return texto;
}

// Devuelve la primera letra del texto, o "X" si el texto esta vacio
string primeraLetra(const string& texto) {
    return texto.empty() ? string("X") : texto.substr(0, 1);
}

// Duplica las comillas simples para que el SQL no se rompa (O'Neil -> O''Neil)
string escapar(const string& texto) {
    string salida;
    for (char c : texto) {
        if (c == '\'') salida += "''";
        else salida += c;
    }
    return salida;
}

// Quita los espacios sobrantes (las columnas nchar de SQL los agregan)
string recortar(const string& texto) {
    size_t fin = texto.find_last_not_of(' ');
    return (fin == string::npos) ? string() : texto.substr(0, fin + 1);
}

// Lee una linea completa de texto
string leerTexto(const string& mensaje) {
    cout << mensaje;
    string linea;
    getline(cin, linea);
    return linea;
}

// Lee un entero de la consola.
// Regresa false si la entrada no es numerica o si la entrada se cerro.
bool leerEntero(int& valor) {
    cin >> valor;
    if (cin.eof()) return false; // Se cerro la entrada: no limpiar
    if (cin.fail()) { // No escribieron un numero
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
        return false;
    }
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    return true;
}

// ==========================================
// CLASE DBConexion: encapsula la conexion ODBC a SQL Server Express
// ==========================================

class DBConexion {
private:
    SQLHENV env = SQL_NULL_HENV; // Entorno ODBC
    SQLHDBC dbc = SQL_NULL_HDBC; // Conexion activa

    // Imprime el mensaje de error que devuelve SQL Server
    static void mostrarErrorODBC(SQLSMALLINT tipo, SQLHANDLE handle) {
        SQLCHAR estado[6];
        SQLCHAR mensaje[SQL_MAX_MESSAGE_LENGTH];
        SQLINTEGER nativo = 0;
        SQLSMALLINT longitud = 0;
        for (SQLSMALLINT i = 1;
             SQLGetDiagRecA(tipo, handle, i, estado, &nativo,
                            mensaje, sizeof(mensaje), &longitud) == SQL_SUCCESS;
             ++i) {
            cerr << "[SQL " << estado << "] " << mensaje << "\n";
        }
    }

public:
    ~DBConexion() { desconectar(); }

    // Abre la conexion con una cadena ODBC. Regresa true si fue exitosa.
    bool conectar(const string& cadena) {
        if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env)))
            return false;
        SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
        if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc)))
            return false;

        SQLCHAR salida[1024];
        SQLSMALLINT lonSalida = 0;
        SQLRETURN r = SQLDriverConnectA(dbc, NULL, (SQLCHAR*)cadena.c_str(), SQL_NTS,
                                        salida, sizeof(salida), &lonSalida,
                                        SQL_DRIVER_NOPROMPT);
        if (!SQL_SUCCEEDED(r)) mostrarErrorODBC(SQL_HANDLE_DBC, dbc);
        return SQL_SUCCEEDED(r);
    }

    // Cierra la conexion y libera los manejadores ODBC
    void desconectar() {
        if (dbc != SQL_NULL_HDBC) {
            SQLDisconnect(dbc);
            SQLFreeHandle(SQL_HANDLE_DBC, dbc);
            dbc = SQL_NULL_HDBC;
        }
        if (env != SQL_NULL_HENV) {
            SQLFreeHandle(SQL_HANDLE_ENV, env);
            env = SQL_NULL_HENV;
        }
    }

    // Ejecuta una instruccion sin resultados (UPDATE, DELETE, etc.)
    // Si SQL Server rechaza la instruccion, imprime el error
    bool ejecutar(const string& sql) {
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
        SQLRETURN r = SQLExecDirectA(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
        if (!SQL_SUCCEEDED(r)) mostrarErrorODBC(SQL_HANDLE_STMT, stmt);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return SQL_SUCCEEDED(r);
    }

    // Ejecuta un SELECT (o un INSERT ... OUTPUT) y regresa las filas como texto.
    // Si SQL Server rechaza la instruccion, imprime el error y regresa vacio.
    vector<vector<string>> consultar(const string& sql) {
        vector<vector<string>> filas;
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt);
        SQLRETURN r = SQLExecDirectA(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
        if (SQL_SUCCEEDED(r)) {
            SQLSMALLINT columnas = 0;
            SQLNumResultCols(stmt, &columnas);
            while (SQL_SUCCEEDED(SQLFetch(stmt))) { // Recorre cada renglon
                vector<string> fila;
                for (SQLUSMALLINT c = 1; c <= columnas; ++c) {
                    char buffer[256];
                    SQLLEN indicador = 0;
                    SQLGetData(stmt, c, SQL_C_CHAR, buffer, sizeof(buffer), &indicador);
                    fila.push_back(indicador == SQL_NULL_DATA ? string("") : string(buffer));
                }
                filas.push_back(fila);
            }
        } else {
            mostrarErrorODBC(SQL_HANDLE_STMT, stmt);
        }
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return filas;
    }
};

// Revisa que el centro exista en el catalogo CentroTrabajo
bool centroExiste(DBConexion& db, const string& codigo) {
    return !db.consultar("SELECT NumeroCentro FROM CentroTrabajo WHERE NumeroCentro = '" +
                         escapar(codigo) + "'").empty();
}

// ==========================================
// CLASE BASE: Empleado
// ==========================================

class Empleado {
protected:
    int numeroEmpleado = 0;      // Autogenerado por SQL Server 
    string nombre;
    string apellidoPaterno;
    string apellidoMaterno;
    string fechaNacimiento;     // Formato AAAA-MM-DD
    string rfc;                 // Calculado con nombre y fecha de nacimiento
    string NumeroCentro;       // Numero de centro, ej. 000201 (nvarchar(6))
    int clavePuesto = 0;        // Clave del catalogo puestos (NumeroPuesto)
    string descripcionPuesto;   // Nombre del puesto, tomado del catalogo
    bool directivo = false;     // Bandera: 1 directivo, 0 empleado normal

    // RFC: inicial AP + primera vocal interna AP + inicial AM + inicial nombre + AAMMDD + homoclave (XXX).
    string calcularRFC() const {
        string ap = aMayusculas(apellidoPaterno);
        string am = aMayusculas(apellidoMaterno);
        string no = aMayusculas(nombre);

        // 1) Primera vocal interna del apellido paterno (si no hay, "X")
        string vocal = "X";
        for (size_t i = 1; i < ap.size(); ++i) {
            if (string("AEIOU").find(ap[i]) != string::npos) {
                vocal = string(1, ap[i]);
                break;
            }
        }

        // 2) Fecha AAAA-MM-DD -> AAMMDD
        string aa = fechaNacimiento.substr(2, 2);
        string mm = fechaNacimiento.substr(5, 2);
        string dd = fechaNacimiento.substr(8, 2);

        // 3) Armado del RFC
        return primeraLetra(ap) + vocal + primeraLetra(am) + primeraLetra(no) +
               aa + mm + dd + "XXX";
    }

public:
    // Constructor: recibe los datos capturados y calcula el RFC
    Empleado(string nom, string ap, string am, string fn, string centro, int clave)
        : nombre(nom), apellidoPaterno(ap), apellidoMaterno(am),
          fechaNacimiento(fn), NumeroCentro(centro), clavePuesto(clave) {
        rfc = calcularRFC();
    }

    virtual ~Empleado() {}

    // Consulta el catalogo de puestos y guarda el nombre del puesto
    bool cargarDescripcionPuesto(DBConexion& db) {
        auto f = db.consultar("SELECT Puesto FROM Puesto WHERE NumeroPuesto = " + to_string(clavePuesto));
        if (f.empty()) return false;
        descripcionPuesto = recortar(f[0][0]);
        return true;
    }

    // INSERT en la tabla Empleado. SQL Server regresa el numero autogenerado.
    virtual bool guardar(DBConexion& db) {
        string sql =
            "INSERT INTO Empleado (Nombre, ApellidoPaterno, ApellidoMaterno, FechaNacimiento, RFC, "
            "NumeroCentro, NumeroPuesto, Directivo) "
            "OUTPUT INSERTED.NumeroEmpleado "
            "VALUES ('" + escapar(nombre) + "', '" +
            escapar(apellidoPaterno) + "', '" +
            escapar(apellidoMaterno) + "', '" +
            escapar(fechaNacimiento) + "', '" +
            escapar(rfc) + "', '" +
            escapar(NumeroCentro) + "', " +
            to_string(clavePuesto) + ", " +
            (directivo ? "1" : "0") + ")";

        auto f = db.consultar(sql);
        if (f.empty()) return false;
        numeroEmpleado = stoi(f[0][0]); // Guarda el numero generado por la BD
        return true;
    }

    // Getters
    int getNumeroEmpleado() const { return numeroEmpleado; }
    string getRFC() const { return rfc; }
    string getDescripcionPuesto() const { return descripcionPuesto; }
};

// ==========================================
// CLASE DERIVADA: Directivo (hereda todo de Empleado)
// ==========================================

class Directivo : public Empleado {
private:
    string numeroCentroSupervisado; // Centro que supervisa el directivo
    bool prestacionCombustible; // 1 si recibe apoyo de combustible, 0 si no es el caso

public:
    Directivo(string nom, string ap, string am, string fn, string centro, int clave,
              string centroSup, bool combustible)
        : Empleado(nom, ap, am, fn, centro, clave), // Llama al constructor base
          numeroCentroSupervisado(centroSup),
          prestacionCombustible(combustible) {
        directivo = true; // Bandera Directivo = 1
    }

    // Primero guarda los datos generales (tabla Empleado) y luego los
    // atributos propios (tabla Directivos).
    bool guardar(DBConexion& db) override {
        if (!Empleado::guardar(db)) return false;     // 1) Datos generales
        string sql =
            "INSERT INTO Directivo (NumeroEmpleado, numeroCentroSupervisa, prestacionCombustible) "
            "VALUES (" + to_string(numeroEmpleado) + ", '" +
            escapar(numeroCentroSupervisado) + "', " +
            (prestacionCombustible ? "1" : "0") + ")";
        return db.ejecutar(sql);                  
    }
};

// ==========================================
// Captura de un empleado desde consola
// ==========================================

// Regresa un unique_ptr<Empleado>; si es directivo, el objeto es un Directivo.
unique_ptr<Empleado> capturarEmpleado(DBConexion& db) {
    string nombre = leerTexto("Nombre: ");
    string ap = leerTexto("Apellido paterno: ");
    string am = leerTexto("Apellido materno: ");
    string fecha = leerTexto("Fecha de nacimiento (AAAA-MM-DD): ");
    if (fecha.size() != 10) {
        cout << "Formato de fecha invalido.\n";
        return nullptr;
    }

    string centro = leerTexto("Centro de trabajo (ej. 000201): ");
    if (centro.size() != 6 || !centroExiste(db, centro)) {
        cout << "El centro de trabajo no existe en el catalogo.\n";
        return nullptr;
    }

    int clave = 0;
    cout << "Clave del puesto: ";
    if (!leerEntero(clave)) {
        cout << "La clave del puesto debe ser un numero.\n";
        return nullptr;
    }

    int esDirectivo = 0;
    cout << "Es directivo? (1 Si, 0 No): ";
    if (!leerEntero(esDirectivo)) {
        cout << "Respuesta invalida.\n";
        return nullptr;
    }

    unique_ptr<Empleado> emp;
    if (esDirectivo == 1) {
        // Atributos propios del directivo
        string centroSupervisa = leerTexto("Numero del centro que supervisa: ");
        if (centroSupervisa.size() != 6 || !centroExiste(db, centroSupervisa)) {
            cout << "El centro que supervisa no existe en el catalogo.\n";
            return nullptr;
        }

        int combustible = 0;
        cout << "Prestacion de combustible (1=Si, 0=No): ";
        if (!leerEntero(combustible)) {
            cout << "Respuesta invalida.\n";
            return nullptr;
        }

        emp = make_unique<Directivo>(nombre, ap, am, fecha, centro, clave,
                                   centroSupervisa, combustible == 1);
    } else {
        emp = make_unique<Empleado>(nombre, ap, am, fecha, centro, clave);
    }

    // Valida la clave y muestra el puesto del catalogo
    if (!emp->cargarDescripcionPuesto(db)) {
        cout << "La clave de puesto no existe en el catalogo.\n";
        return nullptr;
    }

    cout << "Descripcion del puesto: " << emp->getDescripcionPuesto() << "\n";
    cout << "RFC calculado: " << emp->getRFC() << "\n";
    return emp;
}

// ==========================================
// Reporte de empleados
// ==========================================
void imprimirReporte(DBConexion& db) {
    auto filas = db.consultar(
        "SELECT e.NumeroEmpleado, "
        "RTRIM(e.nombre) + ' ' + RTRIM(e.apellidoPaterno) + ' ' + RTRIM(e.apellidoMaterno), "
        "CONVERT(VARCHAR(10), CAST(RTRIM(e.fechaNacimiento) AS DATE), 103), "
        "RTRIM(e.RFC), c.NumeroCentro, p.NumeroPuesto, "
        "CASE WHEN e.directivo = 1 THEN 'Si' ELSE 'No' END "
        "FROM Empleado e "
        "INNER JOIN CentroTrabajo c ON c.NumeroCentro = e.NumeroCentro "
        "INNER JOIN Puesto p ON p.NumeroPuesto = e.NumeroPuesto "
        "ORDER BY e.NumeroEmpleado");

    // Encabezados
    cout << "\n" << left
         << setw(8)  << "Num."
         << setw(28) << "Nombre Completo"
         << setw(21) << "Fecha de Nacimiento"
         << setw(16) << "RFC"
         << setw(22) << "Centro de Trabajo"
         << setw(12) << "Puesto"
         << setw(14) << "Es Directivo?" << "\n";
    cout << string(118, '-') << "\n";

    // Renglones 
    for (const auto& f : filas) {
        cout << left
             << setw(8)  << f[0]
             << setw(28) << f[1]
             << setw(21) << f[2]
             << setw(16) << f[3]
             << setw(22) << f[4]
             << setw(12) << f[5]
             << setw(14) << f[6] << "\n";
    }
    cout << "Total de empleados: " << filas.size() << "\n";
}

// ==========================================
// Programa principal: menu de captura y reporte
// ==========================================

int main() {
    DBConexion db;

    // Cadena de conexion a SQL Server Express (autenticacion de Windows).
    const string cadena =
        "DRIVER={ODBC Driver 17 for SQL Server};"
        "SERVER=DESKTOP-KSS8R1U\\SQLEXPRESS;"
        "DATABASE=UNI_dB;"
        "Trusted_Connection=yes;";

    if (!db.conectar(cadena)) {
        cout << "No fue posible conectar con SQL Server Express.\n";
        return 1;
    }

    cout << "Conexion exitosa a la base de datos de UNI (UNI_dB).\n";

    int opcion = -1;
    do {
        cout << "\n=== MENU ===\n"
             << "1) Capturar empleado\n"
             << "2) Mostrar reporte\n"
             << "0) Salir\n"
             << "Opcion: ";
        opcion = -1;
        if (!leerEntero(opcion)) {
            if (cin.eof()) break; // Entrada cerrada: salir
            cout << "Opcion invalida.\n";
            continue;
        }

        if (opcion == 1) {
            unique_ptr<Empleado> emp = capturarEmpleado(db);
            if (emp && emp->guardar(db)) {
                cout << "Empleado guardado con numero " << emp->getNumeroEmpleado() << ".\n";
            } else {
                cout << "No se pudo guardar el empleado.\n";
            }
        } else if (opcion == 2) {
            imprimirReporte(db);
        } else if (opcion != 0) {
            cout << "Opcion invalida.\n";
        }
    } while (opcion != 0);

    return 0;
}