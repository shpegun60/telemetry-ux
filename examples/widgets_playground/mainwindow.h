/*
 * Qt playground window over the process-lifetime demo catalog.
 *
 * The window owns the generated UI wrapper; Qt parent ownership keeps its
 * widgets and refresh timer alive. All endpoint reads and writes run on the
 * same GUI thread, including periodic simulated measurements.
 *
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#pragma once

#include <QMainWindow>
#include "demo/DemoCatalog.h"

class QPlainTextEdit;
class QTableWidget;

QT_BEGIN_NAMESPACE

namespace Ui {
class MainWindow;
}

QT_END_NAMESPACE

// Public methods:
// - MainWindow(): Build playground widgets.
// - ~MainWindow(): Release UI wrapper.
class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	explicit MainWindow(QWidget* parent = nullptr);
	~MainWindow() override;

private:
	// GUI-thread refresh; endpoint values are sampled once per displayed row.
	void refreshValues();

	Ui::MainWindow* ui;
	QTableWidget* fields_ = nullptr;
	QPlainTextEdit* values_ = nullptr;
};
#endif // MAINWINDOW_H
