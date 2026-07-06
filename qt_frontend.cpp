#include <QApplication>
#include <QFrame>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstring>
#include <vector>

extern "C" {
#include "app_controller.h"
#include "db.h"
#include "dxcluster.h"
#include "globals.h"
#include "qso.h"
#include "stats.h"
}

class LoggerQtWindow : public QMainWindow {
public:
  LoggerQtWindow() {
    setWindowTitle("Logger (Qt)");
    resize(1300, 860);
    setFocusPolicy(Qt::StrongFocus);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(2);

    log_group_ = new QGroupBox("QSO Log", central);
    log_group_->setObjectName("logGroup");
    auto *log_layout = new QVBoxLayout(log_group_);
    log_layout->setContentsMargins(6, 24, 6, 6);
    log_layout->setSpacing(2);
    log_table_ = new QTableWidget(log_group_);
    log_table_->setColumnCount(8);
    log_table_->setHorizontalHeaderLabels(
        {"Nr", "Date", "UTC", "Call", "Freq", "Band", "Mode", "RST"});
    log_table_->verticalHeader()->setVisible(false);
    log_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    log_table_->setSelectionMode(QAbstractItemView::NoSelection);
    log_table_->setFocusPolicy(Qt::NoFocus);
    log_table_->setShowGrid(false);
    log_table_->setWordWrap(false);
    log_table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    log_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    log_table_->horizontalHeader()->setStretchLastSection(false);
    log_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    log_layout->addWidget(log_table_);
    root->addWidget(log_group_);

    suggestions_frame_ = new QFrame(log_group_);
    suggestions_frame_->setFrameShape(QFrame::StyledPanel);
    suggestions_frame_->setFixedSize(320, 170);
    auto *suggest_layout = new QVBoxLayout(suggestions_frame_);
    suggest_layout->setContentsMargins(6, 6, 6, 6);
    suggest_layout->setSpacing(2);
    auto *suggest_title = new QLabel("Call Suggestions", suggestions_frame_);
    suggest_title->setObjectName("suggestTitle");
    suggestions_list_ = new QListWidget(suggestions_frame_);
    suggestions_list_->setFocusPolicy(Qt::NoFocus);
    suggestions_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    suggestions_list_->setUniformItemSizes(true);
    suggestions_list_->setWordWrap(false);
    suggest_layout->addWidget(suggest_title);
    suggest_layout->addWidget(suggestions_list_);
    suggestions_frame_->hide();

    input_panel_ = new QFrame(central);
    auto *input_layout = new QHBoxLayout(input_panel_);
    input_layout->setContentsMargins(8, 6, 8, 6);
    input_label_ = new QLabel("CALL FREQ RST >", input_panel_);
    input_edit_ = new QLineEdit(input_panel_);
    input_edit_->setReadOnly(true);
    input_edit_->setFocusPolicy(Qt::NoFocus);
    input_layout->addWidget(input_label_);
    input_layout->addWidget(input_edit_);
    input_label_->setWordWrap(false);
    input_edit_->setMinimumWidth(250);
    root->addWidget(input_panel_);

    status_panel_ = new QFrame(central);
    auto *status_layout = new QHBoxLayout(status_panel_);
    status_layout->setContentsMargins(8, 2, 8, 2);
    status_label_ = new QLabel(status_panel_);
    info_label_ = new QLabel(status_panel_);
    status_label_->setWordWrap(false);
    info_label_->setWordWrap(false);
    status_layout->addWidget(status_label_, 1);
    status_layout->addWidget(info_label_, 1, Qt::AlignRight);
    root->addWidget(status_panel_);

    dxcc_panel_ = new QFrame(central);
    auto *dxcc_layout = new QHBoxLayout(dxcc_panel_);
    dxcc_layout->setContentsMargins(8, 2, 8, 2);
    dxcc_label_ = new QLabel(dxcc_panel_);
    dxcc_label_->setWordWrap(false);
    dxcc_layout->addWidget(dxcc_label_);
    root->addWidget(dxcc_panel_);

    stats_panel_ = new QFrame(central);
    auto *stats_layout = new QHBoxLayout(stats_panel_);
    stats_layout->setContentsMargins(8, 2, 8, 2);
    stats_label_ = new QLabel(stats_panel_);
    stats_label_->setWordWrap(false);
    stats_layout->addWidget(stats_label_);
    root->addWidget(stats_panel_);

    gap_panel_ = new QFrame(central);
    gap_panel_->setFixedHeight(10);
    root->addWidget(gap_panel_);

    cluster_group_ = new QGroupBox("DX Cluster", central);
    cluster_group_->setObjectName("clusterGroup");
    auto *cluster_layout = new QVBoxLayout(cluster_group_);
    cluster_layout->setContentsMargins(6, 24, 6, 6);
    cluster_layout->setSpacing(2);
    cluster_table_ = new QTableWidget(cluster_group_);
    cluster_table_->setColumnCount(4);
    cluster_table_->setHorizontalHeaderLabels({"Time", "Freq", "Call", "Comment"});
    cluster_table_->verticalHeader()->setVisible(false);
    cluster_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cluster_table_->setSelectionMode(QAbstractItemView::NoSelection);
    cluster_table_->setFocusPolicy(Qt::NoFocus);
    cluster_table_->setShowGrid(false);
    cluster_table_->setWordWrap(false);
    cluster_table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    cluster_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    cluster_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    cluster_layout->addWidget(cluster_table_);
    cluster_group_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    root->addWidget(cluster_group_, 1);

    function_panel_ = new QFrame(central);
    auto *function_layout = new QHBoxLayout(function_panel_);
    function_layout->setContentsMargins(8, 2, 8, 2);
    function_label_ = new QLabel(function_panel_);
    function_label_->setWordWrap(false);
    function_layout->addWidget(function_label_);
    root->addWidget(function_panel_);

    apply_theme();
    apply_character_cell_layout();

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this]() { refresh_ui(); });
    timer_->start(120);

    refresh_ui();
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QMainWindow::resizeEvent(event);
    apply_character_cell_layout();
    place_suggestions_panel();
  }

  void keyPressEvent(QKeyEvent *event) override {
    if (event->key() == Qt::Key_F2) {
      prompt_create_named_log();
      refresh_ui();
      event->accept();
      return;
    }

    if (event->key() == Qt::Key_F3) {
      prompt_open_named_log();
      refresh_ui();
      event->accept();
      return;
    }

    int key = translate_key(event);
    if (key == APP_KEY_NONE) {
      event->ignore();
      return;
    }

    AppControllerEvent ctrl_event = app_controller_handle_key(key);
    if (ctrl_event == APP_CTRL_EVENT_REQUEST_CTY_UPDATE) {
      refresh_ui();
      qApp->processEvents();
      app_controller_perform_cty_update();
    }

    if (ctrl_event == APP_CTRL_EVENT_EXIT)
      close();

    refresh_ui();
    event->accept();
  }

private:
  void submit_command_text(const QString &command_text) {
    // Ensure command entry starts from a clean state; otherwise command text can
    // be appended to partially typed QSO input and won't be recognized.
    app_controller_handle_key(APP_KEY_ESC);

    AppRenderState state;
    app_controller_get_render_state(&state);
    if (state.input) {
      size_t existing_len = std::strlen(state.input);
      for (size_t i = 0; i < existing_len; i++)
        app_controller_handle_key(APP_KEY_BACKSPACE);
    }

    const QByteArray bytes = command_text.toUtf8();
    for (unsigned char ch : bytes)
      app_controller_handle_key((int)ch);

    app_controller_handle_key(APP_KEY_ENTER);
  }

  void prompt_create_named_log() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        "Create New Log",
        "Enter new log name:",
        QLineEdit::Normal,
        "",
        &ok);

    if (!ok)
      return;

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
      QMessageBox::information(this, "Create New Log",
                               "Log name cannot be empty.");
      return;
    }

    submit_command_text(QString("newlog %1").arg(trimmed));
  }

  void prompt_open_named_log() {
    DBNamedLogbook items[128];
    int count = 0;

    if (db_list_named_logbooks(items, 128, &count) != 0) {
      QMessageBox::warning(this, "Open Log",
                           "Cannot read logs from database.");
      return;
    }

    if (count <= 0) {
      QMessageBox::information(this, "Open Log",
                               "No saved logs available in database.");
      return;
    }

    QStringList options;
    for (int i = 0; i < count; i++) {
      options << QString("%1 | %2 | %3 QSO | %4")
                     .arg(items[i].id)
                     .arg(items[i].name)
                     .arg(items[i].qso_count)
                     .arg(items[i].created_at);
    }

    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this,
        "Open Log",
        "Select log to open:",
        options,
        0,
        false,
        &ok);

    if (!ok || chosen.isEmpty())
      return;

    const QString id = chosen.section(" | ", 0, 0).trimmed();
    if (id.isEmpty()) {
      QMessageBox::warning(this, "Open Log",
                           "Invalid log selection.");
      return;
    }

    submit_command_text(QString("openlog %1").arg(id));
  }

  static int translate_key(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_F1:
      return APP_KEY_F1;
    case Qt::Key_F2:
      return APP_KEY_F2;
    case Qt::Key_F3:
      return APP_KEY_F3;
    case Qt::Key_F4:
      return APP_KEY_F4;
    case Qt::Key_F5:
      return APP_KEY_F5;
    case Qt::Key_F6:
      return APP_KEY_F6;
    case Qt::Key_F7:
      return APP_KEY_F7;
    case Qt::Key_F10:
      return APP_KEY_F10;
    case Qt::Key_Up:
      return APP_KEY_UP;
    case Qt::Key_Down:
      return APP_KEY_DOWN;
    case Qt::Key_PageUp:
      return APP_KEY_PAGE_UP;
    case Qt::Key_PageDown:
      return APP_KEY_PAGE_DOWN;
    case Qt::Key_Backspace:
      return APP_KEY_BACKSPACE;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      return APP_KEY_ENTER;
    case Qt::Key_Tab:
      return APP_KEY_TAB;
    case Qt::Key_Escape:
      return APP_KEY_ESC;
    case Qt::Key_Space:
      return APP_KEY_SPACE;
    default:
      break;
    }

    if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
      const QString text = event->text();
      if (text.size() == 1) {
        const QChar ch = text.at(0);
        if (ch.isPrint())
          return ch.toLatin1();
      }
    }

    return APP_KEY_NONE;
  }

  void apply_theme() {
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(10);
    setFont(mono);

    setStyleSheet(
      "QMainWindow, QWidget { background: #89d4e8; color: #0d2d3b; }"
        "QGroupBox { border: 1px solid #0e6a85; border-radius: 3px; margin-top: 8px;"
      "          background: #95dceb; font-weight: bold; }"
      "QGroupBox#logGroup::title { subcontrol-origin: margin; left: 10px;"
"                           padding: 0 6px; color: #f4f8ff; background: #0f5ea4; }"
      "QGroupBox#clusterGroup::title { subcontrol-origin: margin; left: 10px; padding: 0 6px;"
      "                               color: #f4f8ff; background: #0f5ea4; }"
      "QHeaderView::section { background: #0f5ea4; color: #ffe66d; padding: 1px 4px;"
        "                       border: 0; font-weight: bold; }"
      "QTableWidget { background: #9be2ef; gridline-color: #5ca9bf; }"
        "QLineEdit { background: #9be2ef; border: 1px solid #0e6a85; color: #0d2d3b; }"
        "QFrame#suggestTitle { color: #f4f8ff; }"
        "QListWidget { background: #9be2ef; border: 1px solid #0e6a85; }"
        "QListWidget::item:selected { background: #f3d24f; color: #111; }");

    input_panel_->setStyleSheet("QFrame { background: #9be2ef; border: 1px solid #0e6a85; }");
    status_panel_->setStyleSheet("QFrame { background: #0f5ea4; color: #f4f8ff; }");
    dxcc_panel_->setStyleSheet("QFrame { background: #0f5ea4; color: #ffe66d; font-weight: bold; }");
    stats_panel_->setStyleSheet("QFrame { background: #0f5ea4; color: #ffe66d; font-weight: bold; }");
    gap_panel_->setStyleSheet("QFrame { background: #95dceb; border: 0; }");
    function_panel_->setStyleSheet("QFrame { background: #0f5ea4; color: #f4f8ff; font-weight: bold; }");
    suggestions_frame_->setStyleSheet(
        "QFrame { background: #95dceb; border: 1px solid #0e6a85; }"
        "QLabel#suggestTitle { background: #0f5ea4; color: #f4f8ff; padding: 2px 6px; font-weight: bold; }");
  }

  int cols_to_px(int cols, int padding = 10) const {
    return cols * cell_w_ + padding;
  }

  QString clip_to_cols(const QString &text, int cols) const {
    if (cols <= 0)
      return QString();

    return text.left(cols);
  }

  QString clip_cstr_to_cols(const char *text, int cols) const {
    if (!text || cols <= 0)
      return QString();

    const int n = std::min(cols, (int)std::strlen(text));
    return QString::fromLatin1(text, n);
  }

  int panel_available_cols(const QWidget *panel, int inner_padding_px = 16) const {
    if (!panel)
      return 0;

    const int px = std::max(0, panel->width() - inner_padding_px);
    return px / std::max(1, cell_w_);
  }

  void apply_character_cell_layout() {
    QFontMetrics fm(font());
    cell_w_ = std::max(7, fm.horizontalAdvance(QLatin1Char('M')));
    cell_h_ = std::max(14, fm.height());

    const int row_h = cell_h_ + 2;
    const int header_h = cell_h_ + 4;
    const int line_panel_h = cell_h_ + 8;

    log_table_->verticalHeader()->setDefaultSectionSize(row_h);
    log_table_->horizontalHeader()->setFixedHeight(header_h);
    cluster_table_->verticalHeader()->setDefaultSectionSize(row_h);
    cluster_table_->horizontalHeader()->setFixedHeight(header_h);

    const int log_col_chars[8] = {3, 9, 5, 15, 6, 5, 5, 4};
    for (int i = 0; i < 8; i++) {
      if (i == 3)
        continue;
      log_table_->setColumnWidth(i, cols_to_px(log_col_chars[i]));
    }

    const int cluster_col_chars[4] = {9, 11, 13, 20};
    cluster_table_->setColumnWidth(0, cols_to_px(cluster_col_chars[0]));
    cluster_table_->setColumnWidth(1, cols_to_px(cluster_col_chars[1]));
    cluster_table_->setColumnWidth(2, cols_to_px(cluster_col_chars[2]));

    log_group_->setMinimumHeight((12 + 3) * row_h + 26);

    input_panel_->setFixedHeight(line_panel_h + 4);
    status_panel_->setFixedHeight(line_panel_h);
    dxcc_panel_->setFixedHeight(line_panel_h);
    stats_panel_->setFixedHeight(line_panel_h);
    gap_panel_->setFixedHeight(line_panel_h / 2);
    function_panel_->setFixedHeight(line_panel_h);
  }

  void place_suggestions_panel() {
    if (!log_group_)
      return;

    const int total_cols = std::max(1, log_group_->width() / std::max(1, cell_w_));
    const int suggest_cols = std::clamp(total_cols / 3, 24, 40);
    const int suggest_rows = 8;
    const int suggest_w = cols_to_px(suggest_cols, 14);
    const int suggest_h = (suggest_rows + 2) * (cell_h_ + 2) + 8;
    suggestions_frame_->setFixedSize(suggest_w, suggest_h);

    const int x = std::max(8, log_group_->width() - suggestions_frame_->width() - 12);
    const int y = 26;
    suggestions_frame_->move(x, y);
    suggestions_frame_->raise();
  }

  void refresh_log_table() {
    if (qso_count <= 0) {
      log_table_->setRowCount(1);
      auto *item = new QTableWidgetItem("No QSOs");
      item->setTextAlignment(Qt::AlignCenter);
      log_table_->setSpan(0, 0, 1, log_table_->columnCount());
      log_table_->setItem(0, 0, item);
      return;
    }

    const int max_rows = 200;
    const int start = std::max(0, qso_count - max_rows);
    const int rows = qso_count - start;
    log_table_->clearSpans();
    log_table_->setRowCount(rows);

    for (int row = 0; row < rows; row++) {
      const int idx = start + row;
      const QSO &q = logbook[idx];

      auto set_item = [&](int col, const QString &text) {
        auto *cell = new QTableWidgetItem(text);
          const int hard_cols[8] = {3, 8, 4, 14, 5, 4, 4, 3};
          cell->setText(clip_to_cols(cell->text(), hard_cols[col]));
          cell->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);

        if (q.invalid) {
          cell->setBackground(QColor(250, 224, 110));
          cell->setForeground(QColor(20, 20, 20));
        }
        log_table_->setItem(row, col, cell);
      };

      set_item(0, QString::number(idx + 1));
      set_item(1, q.date);
      set_item(2, q.utc);
      set_item(3, q.call);
      set_item(4, QString::number(q.freq));
      set_item(5, q.band);
      set_item(6, q.mode);
      set_item(7, q.rst);
    }

    log_table_->scrollToBottom();
  }

  int refresh_cluster_table(bool fullscreen_mode, int scroll) {
    struct SpotCopy {
      QString time;
      QString freq;
      QString call;
      QString comment;
    };

    std::vector<SpotCopy> items;
    QString status;

    pthread_mutex_lock(&dxcluster_mutex);
    status = dxcluster_status;

    int start = spot_start;
    int count = spot_count;
    int max_scroll = 0;

    if (!fullscreen_mode) {
      const int visible = 70;
      if (count > visible) {
        start = (spot_start + count - visible) % MAX_SPOTS;
        count = visible;
      }
    } else {
      int visible = cluster_table_->viewport()->height() /
                    std::max(1, cluster_table_->verticalHeader()->defaultSectionSize());
      if (visible < 1)
        visible = 1;

      max_scroll = count > visible ? count - visible : 0;
      if (scroll < 0)
        scroll = 0;
      if (scroll > max_scroll)
        scroll = max_scroll;

      const int offset = count > visible ? count - visible - scroll : 0;
      start = (spot_start + offset) % MAX_SPOTS;
      count = std::min(count, visible);
    }

    items.reserve(count);
    for (int i = 0; i < count; i++) {
      const int idx = (start + i) % MAX_SPOTS;
      items.push_back({spots[idx].time, spots[idx].freq, spots[idx].call, spots[idx].comment});
    }
    pthread_mutex_unlock(&dxcluster_mutex);

    cluster_group_->setTitle(QString("DX Cluster [%1]").arg(status));

    cluster_table_->setRowCount((int)items.size());
    for (int row = 0; row < (int)items.size(); row++) {
      auto *time_item = new QTableWidgetItem(items[row].time);
      auto *freq_item = new QTableWidgetItem(items[row].freq);
      auto *call_item = new QTableWidgetItem(items[row].call);
      auto *comment_item = new QTableWidgetItem(items[row].comment);

      time_item->setText(clip_to_cols(time_item->text(), 8));
      freq_item->setText(clip_to_cols(freq_item->text(), 10));
      call_item->setText(clip_to_cols(call_item->text(), 12));
      comment_item->setText(clip_to_cols(comment_item->text(), 80));

      time_item->setTextAlignment(Qt::AlignCenter);
      freq_item->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
      call_item->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
      comment_item->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);

      time_item->setForeground(QColor(14, 90, 20));
      freq_item->setForeground(QColor(14, 90, 20));
      call_item->setForeground(QColor(14, 90, 20));
      comment_item->setForeground(QColor(14, 90, 20));

      cluster_table_->setItem(row, 0, time_item);
      cluster_table_->setItem(row, 1, freq_item);
      cluster_table_->setItem(row, 2, call_item);
      cluster_table_->setItem(row, 3, comment_item);
    }

    if (!fullscreen_mode)
      cluster_table_->scrollToBottom();

    return max_scroll;
  }

  void refresh_suggestions() {
    if (!call_suggestion_available || call_suggestion_count <= 0) {
      suggestions_frame_->hide();
      input_panel_->setStyleSheet("QFrame { background: #9be2ef; border: 1px solid #0e6a85; }");
      return;
    }

    suggestions_frame_->show();
    place_suggestions_panel();

    input_panel_->setStyleSheet("QFrame { background: #f3d24f; border: 1px solid #0e6a85; }");

    suggestions_list_->clear();
    for (int i = 0; i < call_suggestion_count; i++)
      suggestions_list_->addItem(call_suggestion_matches[i]);

    if (call_suggestion_selected_index >= 0 &&
        call_suggestion_selected_index < suggestions_list_->count()) {
      suggestions_list_->setCurrentRow(call_suggestion_selected_index);
      suggestions_list_->scrollToItem(suggestions_list_->item(call_suggestion_selected_index));
    }
  }

  void refresh_ui() {
    AppRenderState state;
    app_controller_get_render_state(&state);

    const int input_cols = panel_available_cols(input_panel_, 26 + cols_to_px(15));
    const int status_cols = panel_available_cols(status_panel_, 16) / 2;
    const int info_cols = panel_available_cols(status_panel_, 16) / 2;
    const int dxcc_cols = panel_available_cols(dxcc_panel_, 16);
    const int stats_cols = panel_available_cols(stats_panel_, 16);
    const int func_cols = panel_available_cols(function_panel_, 16);

    input_edit_->setText(clip_cstr_to_cols(state.input, input_cols));
    status_label_->setText(clip_to_cols(
      QString("Status: %1").arg(state.status ? state.status : ""),
      std::max(1, status_cols)));
    info_label_->setText(clip_to_cols(
      QString("Info: %1").arg(state.info ? state.info : ""),
      std::max(1, info_cols)));
    dxcc_label_->setText(clip_to_cols(QString("DXCC: %1  CQ:%2 ITU:%3")
                        .arg(state.dxcc ? state.dxcc : "")
                             .arg(last_cq)
                        .arg(last_itu),
                      std::max(1, dxcc_cols)));
    stats_label_->setText(clip_to_cols(
      QString("QSO:%1 DXCC:%2  CW:%3 SSB:%4 FT8:%5 FT4:%6 RTTY:%7 PSK31:%8")
        .arg(stats.total_qso)
        .arg(stats.total_dxcc)
        .arg(stats.cw)
        .arg(stats.ssb)
        .arg(stats.ft8)
        .arg(stats.ft4)
        .arg(stats.rtty)
        .arg(stats.psk31),
      std::max(1, stats_cols)));

    if (cty_update_in_progress) {
        function_label_->setText(
          clip_to_cols("F7 CTY update in progress... keyboard locked",
                 std::max(1, func_cols)));
      function_panel_->setStyleSheet(
          "QFrame { background: #f3d24f; color: #111; font-weight: bold; }");
    } else {
        function_label_->setText(clip_to_cols(
      "F1 Help  F2 New Named Log  F3 Open Log  F4 Export  F5 DXCluster  F6 Stats  F7 CTY  F10 Quit",
          std::max(1, func_cols)));
      function_panel_->setStyleSheet(
          "QFrame { background: #0f5ea4; color: #f4f8ff; font-weight: bold; }");
    }

    refresh_log_table();
    const int max_scroll = refresh_cluster_table(state.cluster_view, state.cluster_scroll);
    refresh_suggestions();

    const bool fullscreen_cluster = state.cluster_view;
    log_group_->setVisible(!fullscreen_cluster);
    input_panel_->setVisible(!fullscreen_cluster);
    status_panel_->setVisible(!fullscreen_cluster);
    dxcc_panel_->setVisible(!fullscreen_cluster);
    stats_panel_->setVisible(!fullscreen_cluster);
    gap_panel_->setVisible(!fullscreen_cluster);
    suggestions_frame_->setVisible(!fullscreen_cluster && call_suggestion_available);

    if (fullscreen_cluster) {
      const int display_scroll = std::clamp(state.cluster_scroll, 0, max_scroll);
      function_label_->setText(clip_to_cols(
          QString("UP/DOWN scroll  PgUp/PgDn page  F4 return  %1/%2")
              .arg(display_scroll + 1)
              .arg(max_scroll + 1),
          std::max(1, func_cols)));
    }
  }

  QTimer *timer_ = nullptr;

  QGroupBox *log_group_ = nullptr;
  QTableWidget *log_table_ = nullptr;

  QFrame *suggestions_frame_ = nullptr;
  QListWidget *suggestions_list_ = nullptr;

  QFrame *input_panel_ = nullptr;
  QLabel *input_label_ = nullptr;
  QLineEdit *input_edit_ = nullptr;

  QFrame *status_panel_ = nullptr;
  QLabel *status_label_ = nullptr;
  QLabel *info_label_ = nullptr;

  QFrame *dxcc_panel_ = nullptr;
  QLabel *dxcc_label_ = nullptr;

  QFrame *stats_panel_ = nullptr;
  QLabel *stats_label_ = nullptr;

  QFrame *gap_panel_ = nullptr;

  QGroupBox *cluster_group_ = nullptr;
  QTableWidget *cluster_table_ = nullptr;

  QFrame *function_panel_ = nullptr;
  QLabel *function_label_ = nullptr;

  int cell_w_ = 8;
  int cell_h_ = 16;
};

int main(int argc, char **argv) {
  QApplication app(argc, argv);

  app_controller_init();

  LoggerQtWindow window;
  window.show();

  const int rc = app.exec();

  app_controller_shutdown();
  return rc;
}
