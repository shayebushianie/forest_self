# Home Plant Popup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the modal home plant selector with a dismissible frameless popup that only commits an explicit plant-card selection.

**Architecture:** `MainWindow` owns a guarded pointer to one heap-allocated popup. Qt's `Qt::Popup` window semantics handle outside-click and Escape dismissal; existing card callbacks remain the only state mutation path.

**Tech Stack:** C++17, Qt 6 Widgets, QTest, CMake/CTest.

## Global Constraints

- Preserve current plant unlock logic, persistence fields, and `PlantCatalog`.
- Do not animate popup entry or exit.
- Do not commit, push, package, or modify user data.

---

### Task 1: Popup cancellation regression

**Files:**
- Modify: `src/tests/UiSmokeTest.cpp`
- Modify: `src/ui/MainWindow.h`
- Modify: `src/ui/MainWindow.cpp`

**Interfaces:**
- Consumes: `PlantTimerWidget::sig_plantClicked`, `SettingsPage::focusSettings()`.
- Produces: one `QPointer<QDialog>`-guarded home plant popup.

- [x] **Step 1: Write the failing test**

Open the plant selector through `timerPlantAction`, assert that the active popup has
`Qt::Popup` and is not modal, click a blank point in `MainWindow`, then assert the popup
is closed and `focusPlantSelector` retains its prior data. Repeat with Escape. Finally
click a different `plantCard` and assert that its plant type is committed.

- [x] **Step 2: Run the focused UI test**

Run:

```powershell
cmake --build build-ui-simple --config Release --target forest_ui_smoketest --parallel 4
ctest --test-dir build-ui-simple -C Release -R forest_ui_smoketest --output-on-failure
```

Expected: FAIL because the current selector is modal and blocks outside clicks.

- [x] **Step 3: Implement the popup**

Add `QPointer<QDialog> homePlantSelector_` to `MainWindow`. In
`showHomePlantSelector()`, reuse an existing visible popup; otherwise allocate a dialog
with `Qt::Popup | Qt::FramelessWindowHint`, `WA_DeleteOnClose`, and no modality. Preserve
the current card construction. Card callbacks remain the only code that changes
`focus.plantType`; close the popup after selection.

- [x] **Step 4: Run focused and full verification**

Run:

```powershell
ctest --test-dir build-ui-simple -C Release -R forest_ui_smoketest --output-on-failure
cmake --build build-ui-simple --config Release --parallel 4
ctest --test-dir build-ui-simple -C Release --output-on-failure
```

Expected: all tests pass. Update `MEMORY.md`, deploy the verified `forest.exe` to the
workspace root, verify SHA-256 equality, and launch it. Do not create a Git commit.
