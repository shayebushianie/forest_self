/****************************************************************************
** Meta object code from reading C++ file 'FocusController.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.5.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/core/FocusController.h"
#include <QtCore/qmetatype.h>

#if __has_include(<QtCore/qtmochelpers.h>)
#include <QtCore/qtmochelpers.h>
#else
QT_BEGIN_MOC_NAMESPACE
#endif


#include <memory>

#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'FocusController.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.5.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {

#ifdef QT_MOC_HAS_STRINGDATA
struct qt_meta_stringdata_CLASSFocusControllerENDCLASS_t {};
static constexpr auto qt_meta_stringdata_CLASSFocusControllerENDCLASS = QtMocHelpers::stringData(
    "FocusController",
    "sig_stateChanged",
    "",
    "FocusController::State",
    "newState",
    "sig_tick",
    "uint32_t",
    "displaySeconds",
    "isStopwatch",
    "sig_growthStageChanged",
    "stage",
    "sig_strictWarningTick",
    "remainingWarnSeconds",
    "sig_softViolation",
    "appName"
);
#else  // !QT_MOC_HAS_STRING_DATA
struct qt_meta_stringdata_CLASSFocusControllerENDCLASS_t {
    uint offsetsAndSizes[30];
    char stringdata0[16];
    char stringdata1[17];
    char stringdata2[1];
    char stringdata3[23];
    char stringdata4[9];
    char stringdata5[9];
    char stringdata6[9];
    char stringdata7[15];
    char stringdata8[12];
    char stringdata9[23];
    char stringdata10[6];
    char stringdata11[22];
    char stringdata12[21];
    char stringdata13[18];
    char stringdata14[8];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_CLASSFocusControllerENDCLASS_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_CLASSFocusControllerENDCLASS_t qt_meta_stringdata_CLASSFocusControllerENDCLASS = {
    {
        QT_MOC_LITERAL(0, 15),  // "FocusController"
        QT_MOC_LITERAL(16, 16),  // "sig_stateChanged"
        QT_MOC_LITERAL(33, 0),  // ""
        QT_MOC_LITERAL(34, 22),  // "FocusController::State"
        QT_MOC_LITERAL(57, 8),  // "newState"
        QT_MOC_LITERAL(66, 8),  // "sig_tick"
        QT_MOC_LITERAL(75, 8),  // "uint32_t"
        QT_MOC_LITERAL(84, 14),  // "displaySeconds"
        QT_MOC_LITERAL(99, 11),  // "isStopwatch"
        QT_MOC_LITERAL(111, 22),  // "sig_growthStageChanged"
        QT_MOC_LITERAL(134, 5),  // "stage"
        QT_MOC_LITERAL(140, 21),  // "sig_strictWarningTick"
        QT_MOC_LITERAL(162, 20),  // "remainingWarnSeconds"
        QT_MOC_LITERAL(183, 17),  // "sig_softViolation"
        QT_MOC_LITERAL(201, 7)   // "appName"
    },
    "FocusController",
    "sig_stateChanged",
    "",
    "FocusController::State",
    "newState",
    "sig_tick",
    "uint32_t",
    "displaySeconds",
    "isStopwatch",
    "sig_growthStageChanged",
    "stage",
    "sig_strictWarningTick",
    "remainingWarnSeconds",
    "sig_softViolation",
    "appName"
};
#undef QT_MOC_LITERAL
#endif // !QT_MOC_HAS_STRING_DATA
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_CLASSFocusControllerENDCLASS[] = {

 // content:
      11,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   44,    2, 0x06,    1 /* Public */,
       5,    2,   47,    2, 0x06,    3 /* Public */,
       9,    1,   52,    2, 0x06,    6 /* Public */,
      11,    1,   55,    2, 0x06,    8 /* Public */,
      13,    1,   58,    2, 0x06,   10 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 6, QMetaType::Bool,    7,    8,
    QMetaType::Void, 0x80000000 | 6,   10,
    QMetaType::Void, 0x80000000 | 6,   12,
    QMetaType::Void, QMetaType::QString,   14,

       0        // eod
};

Q_CONSTINIT const QMetaObject FocusController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_CLASSFocusControllerENDCLASS.offsetsAndSizes,
    qt_meta_data_CLASSFocusControllerENDCLASS,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_CLASSFocusControllerENDCLASS_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<FocusController, std::true_type>,
        // method 'sig_stateChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<FocusController::State, std::false_type>,
        // method 'sig_tick'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'sig_growthStageChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        // method 'sig_strictWarningTick'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<uint32_t, std::false_type>,
        // method 'sig_softViolation'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>
    >,
    nullptr
} };

void FocusController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<FocusController *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->sig_stateChanged((*reinterpret_cast< std::add_pointer_t<FocusController::State>>(_a[1]))); break;
        case 1: _t->sig_tick((*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 2: _t->sig_growthStageChanged((*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[1]))); break;
        case 3: _t->sig_strictWarningTick((*reinterpret_cast< std::add_pointer_t<uint32_t>>(_a[1]))); break;
        case 4: _t->sig_softViolation((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (FocusController::*)(FocusController::State );
            if (_t _q_method = &FocusController::sig_stateChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (FocusController::*)(uint32_t , bool );
            if (_t _q_method = &FocusController::sig_tick; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (FocusController::*)(uint32_t );
            if (_t _q_method = &FocusController::sig_growthStageChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (FocusController::*)(uint32_t );
            if (_t _q_method = &FocusController::sig_strictWarningTick; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (FocusController::*)(const QString & );
            if (_t _q_method = &FocusController::sig_softViolation; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
    }
}

const QMetaObject *FocusController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *FocusController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_CLASSFocusControllerENDCLASS.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int FocusController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void FocusController::sig_stateChanged(FocusController::State _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void FocusController::sig_tick(uint32_t _t1, bool _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void FocusController::sig_growthStageChanged(uint32_t _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void FocusController::sig_strictWarningTick(uint32_t _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void FocusController::sig_softViolation(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}
QT_WARNING_POP
