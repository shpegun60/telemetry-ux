		auto num = [](const QVariantMap& map, const char* key) { return map.value(key).toDouble(); };
		auto str = [](const QVariantMap& map, const char* key) { return map.value(key).toString(); };
		const double step16 = 16, pad = 6;
		Q_UNUSED(step16) Q_UNUSED(pad)
		const double bodyPx = rig.call("bodyPixels").toDouble();
		const double indent = rig.call("indentPixels").toDouble();
		const double padding = rig.call("paddingPixels").toDouble();
		CHECK(bodyPx > 8 && indent > 8 && padding > 2);

		// 1. One root per table.
		{
			auto root = rig.row(0);
			CHECK(str(root, "name") == QStringLiteral("Config") && str(root, "type") == QStringLiteral("struct (4)"));
			CHECK(root.value("heading").toBool());
			CHECK(str(root, "valueText").isEmpty() && str(root, "summary").isEmpty());
			CHECK(root.value("expanded").toBool());
			auto calibRow = rig.row(rig.rowOf(QStringLiteral("Calib")));
			CHECK(!calibRow.value("heading").toBool());
			// 2. Defaults: the root open, Calib folded: 5 rows.
			CHECK(rig.call("rowCount").toInt() == 5);
			CHECK(!calibRow.value("expanded").toBool());
			rig.call("use", QStringLiteral("service"));
			CHECK(rig.names() == (QStringList{QStringLiteral("value"), QStringLiteral("limits")}));
			CHECK(rig.call("headingCount", QStringLiteral("valueSectionHeading")).toInt() == 1);
			CHECK(!rig.row(0).value("heading").toBool());
			rig.call("use", QStringLiteral("single"));
			CHECK(rig.call("rowCount").toInt() == 3 && rig.row(0).value("expanded").toBool());
			rig.call("use", QStringLiteral("field"));
			CHECK(rig.call("rowCount").toInt() == 5);
		}

		// 3. Indent at 560, 360 and 320 px: name x = padding + step * depth + indent.
		rig.select(0);
		rig.key(Qt::Key_Asterisk);
		const int allRows = rig.call("rowCount").toInt();
		if (allRows != 25) qWarning() << allRows << rig.names();
		CHECK(allRows == 25);
		const double gap = rig.call("gapPixels").toDouble();
		for (int width : {560, 360, 320}) {
			rig.resizeTo(width);
			rig.call("select", 0);
			// The step the design's rule gives from the measured columns: 16 px
			// while the deepest level keeps 80 px for its name.
			const double nameEnd = num(rig.row(0), "cellSceneX") - num(rig.row(0), "treeX") - gap;
			const double step = std::max(std::floor(indent / 2), std::min(indent, std::floor((nameEnd - (padding + indent) - 6 * bodyPx) / 4)));
			if (width >= 360) CHECK(step == indent);
			for (const auto& item : rig.rows()) {
				const auto row = item.toMap();
				const double depth = num(row, "depth");
				if (std::abs(num(row, "nameX") - (padding + step * depth + indent)) >= 0.6) qWarning() << width << step << nameEnd << row.value("name") << depth << num(row, "nameX");
				CHECK(std::abs(num(row, "nameX") - (padding + step * depth + indent)) < 0.6);
				CHECK(std::abs(num(row, "chevronX") - (padding + step * depth)) < 0.6);
				CHECK(row.value("chevronVisible").toBool() == row.value("hasChildren").toBool());
			}
		}
		rig.resizeTo(560);
		rig.shot(out, QStringLiteral("details-tree-560-open.png"));
		rig.resizeTo(360);
		rig.shot(out, QStringLiteral("details-tree-360-open.png"));
		rig.resizeTo(560);

		// 5. Alignment: the value cell and the type label stand where the header's do.
		auto aligned = [&](int width) {
			rig.resizeTo(width);
			const auto valueHead = rig.call("headerX", QStringLiteral("Value")).toMap();
			const auto typeHead = rig.call("headerX", QStringLiteral("Type")).toMap();
			CHECK(!valueHead.isEmpty());
			for (const auto& item : rig.rows()) {
				const auto row = item.toMap();
				CHECK(std::abs(num(row, "cellSceneX") - num(valueHead, "x")) < 1.5);
				CHECK(std::abs(num(row, "cellW") - num(valueHead, "w")) < 1.5);
				if (!typeHead.isEmpty())
					CHECK(std::abs(num(row, "typeSceneX") - num(typeHead, "x")) < 1.5);
			}
		};
		for (int width : {560, 360, 280, 236})
			aligned(width);

		// 4. Compression: narrow enough for the deepest level to want its 80 px,
		// the step shrinks evenly (to 8 px at the least), and no name is narrower
		// than four characters.
		rig.resizeTo(280);
		rig.shot(out, QStringLiteral("details-tree-280-deep.png"));
		rig.resizeTo(236);
		{
			double step = 0;
			for (const auto& item : rig.rows()) {
				const auto row = item.toMap();
				const double depth = num(row, "depth");
				if (depth == 1) step = num(row, "nameX") - (padding + indent);
			}
			const double nameEnd280 = num(rig.row(0), "cellSceneX") - num(rig.row(0), "treeX") - gap;
			const double expected = std::max(std::floor(indent / 2), std::min(indent, std::floor((nameEnd280 - (padding + indent) - 6 * bodyPx) / 4)));
			CHECK(step < indent && step >= std::floor(indent / 2) && step == expected);
			for (const auto& item : rig.rows()) {
				const auto row = item.toMap();
				const double depth = num(row, "depth");
				CHECK(std::abs(num(row, "nameX") - (padding + indent + step * depth)) < 0.6);
				CHECK(num(row, "nameW") >= 4 * bodyPx - 1);
			}
		}
		rig.call("use", QStringLiteral("deep"));
		rig.select(0);
		rig.key(Qt::Key_Asterisk);
		CHECK(rig.call("rowCount").toInt() == 7);
		for (int width : {560, 236}) {
			rig.resizeTo(width);
			double previousX = -1;
			for (const auto& item : rig.rows()) {
				const auto row = item.toMap();
				CHECK(num(row, "nameW") >= 4 * bodyPx - 1);
				CHECK(num(row, "nameX") > previousX);
				previousX = num(row, "nameX");
			}
			aligned(width);
		}
		CHECK(str(rig.row(6), "pathTip").startsWith(QStringLiteral("device.Deep.A.B.C.D.E.F")));
		// An open editor on the deepest row moves neither the value cell nor the type.
		rig.resizeTo(236);
		CHECK(rig.call("editorOpen", 6).toBool());
		aligned(236);
		CHECK(rig.row(6).value("editor").toBool());
		QTest::keyClick(window, Qt::Key_Escape);
		settle();
		rig.resizeTo(560);
		rig.call("use", QStringLiteral("field"));

		// 6. Indent guides: a row at depth d draws d of them; the current branch's is outline.
		rig.select(0);
		rig.key(Qt::Key_Asterisk);
		{
			const auto head = rig.row(0);
			CHECK(head.value("guides").toList().isEmpty());
			const int offset = rig.rowOf(QStringLiteral("Offset"));
			rig.select(offset);
			const auto outline = rig.row(offset).value("outline").toString();
			for (const auto& item : rig.rows()) {
				const auto row = item.toMap();
				const auto guides = row.value("guides").toList();
				CHECK(guides.size() == int(num(row, "depth")));
				const double step = indent;
				for (int level = 0; level < guides.size(); ++level) {
					const auto guide = guides[level].toMap();
					CHECK(num(guide, "w") == 1 && std::abs(num(guide, "h") - num(row, "height")) < 0.6);
					CHECK(num(guide, "x") == std::round(padding + level * step + indent / 2));
				}
			}
			// Offset is current: its branch is [0] (depth 3): the level-3 guide on Offset and Scale under it.
			const auto first = rig.row(offset), second = rig.row(offset + 1);
			CHECK(first.value("guides").toList()[3].toMap().value("color").toString() == outline);
			CHECK(second.value("guides").toList()[3].toMap().value("color").toString() == outline);
			CHECK(first.value("guides").toList()[0].toMap().value("color").toString() != outline);
			CHECK(first.value("guides").toList()[1].toMap().value("color").toString() != outline);
			CHECK(rig.row(offset + 2).value("guides").toList()[2].toMap().value("color").toString() != outline);
			// Mode is current: Calib's level-1 guides light up on every row under it.
			const int mode = rig.rowOf(QStringLiteral("Mode"));
			rig.select(mode);
			for (int r = rig.rowOf(QStringLiteral("Calib")) + 1; r < rig.call("rowCount").toInt(); ++r) {
				const auto row = rig.row(r);
				CHECK(row.value("guides").toList()[1].toMap().value("color").toString() == outline);
				CHECK(row.value("guides").toList()[0].toMap().value("color").toString() != outline);
			}
			rig.select(offset);
			rig.shot(out, QStringLiteral("details-tree-560-edited.png"));
		}

		// 7. The folded summary.
		rig.select(0);
		rig.key(Qt::Key_Left);
		rig.call("use", QStringLiteral("other"));
		rig.call("use", QStringLiteral("field"));
		rig.call("reread");
		rig.select(0);
		rig.shot(out, QStringLiteral("details-tree-560-default.png"));
		for (int width : {560, 360}) {
			rig.resizeTo(width);
			auto calibRow = rig.row(rig.rowOf(QStringLiteral("Calib")));
			const auto text = str(calibRow, "summary");
			if (width == 560) CHECK(text == QStringLiteral("{Mode Run, Taps […], Phase […]}"));
			else CHECK(text == QStringLiteral("{Mode Run, Taps […], …}"));
			CHECK(text.endsWith(QLatin1Char('}')));
			CHECK(std::abs(num(calibRow, "summaryRight") - (num(calibRow, "cellSceneX") + num(calibRow, "cellW"))) < 1.5);
			CHECK(num(calibRow, "summaryLeft") >= num(calibRow, "nameTextEnd") + 11);
			CHECK(str(calibRow, "valueText").isEmpty());
		}
		rig.resizeTo(360);
		rig.shot(out, QStringLiteral("details-tree-360-default.png"));
		const double seenBefore = rig.call("valueSeen").toDouble();
		rig.select(0);
		rig.key(Qt::Key_Left);
		{
			auto root = rig.row(0);
			CHECK(!root.value("expanded").toBool());
			// Whole parts only, as many as fit, then the tail.
			auto wholeParts = [&](const QString& text) {
				if (!text.startsWith(QLatin1Char('{')) || !text.endsWith(QLatin1Char('}')))
					return false;
				const auto inner = text.mid(1, text.size() - 2).split(QStringLiteral(", "));
				const QStringList parts{QStringLiteral("Gain 1.5"), QStringLiteral("Enabled true"),
				                        QStringLiteral("SamplePeriodMs 100"), QStringLiteral("Calib {…}")};
				for (int i = 0; i < inner.size(); ++i)
					if (!(inner[i] == parts[i] || (i == inner.size() - 1 && inner[i] == QStringLiteral("…"))))
						return false;
				return true;
			};
			CHECK(wholeParts(str(root, "summary")) && str(root, "summary").startsWith(QStringLiteral("{Gain 1.5")));
			const auto narrow = str(root, "summary").size();
			rig.resizeTo(560);
			root = rig.row(0);
			CHECK(wholeParts(str(root, "summary")) && str(root, "summary").size() >= narrow);
			CHECK(rig.call("valueSeen").toDouble() == seenBefore);
			rig.shot(out, QStringLiteral("details-tree-560-folded.png"));
			rig.resizeTo(360);
			rig.shot(out, QStringLiteral("details-tree-360-folded.png"));
		}
		rig.key(Qt::Key_Right);
		CHECK(rig.row(0).value("expanded").toBool());
		rig.key(Qt::Key_Right);
		CHECK(rig.current() == QStringLiteral("Gain"));
		{
			// Taps, opened, folded back: its summary shows its first values.
			rig.select(rig.rowOf(QStringLiteral("Calib")));
			rig.key(Qt::Key_Right);
			const int taps = rig.rowOf(QStringLiteral("Taps"));
			const auto text = str(rig.row(taps), "summary");
			CHECK(text.startsWith(QStringLiteral("[0, 0")) && text.endsWith(QStringLiteral("64 items]")));
			CHECK(str(rig.row(rig.rowOf(QStringLiteral("Phase"))), "summary") == QStringLiteral("[3 items]"));
			CHECK(str(rig.row(rig.rowOf(QStringLiteral("Calib"))), "summary").isEmpty());
		}

		// 8. Keys.
		rig.resizeTo(560);
		rig.call("reread");
		rig.call("select", rig.rowOf(QStringLiteral("Calib")));
		rig.key(Qt::Key_Right);
		CHECK(rig.row(rig.rowOf(QStringLiteral("Calib"))).value("expanded").toBool());
		rig.key(Qt::Key_Right);
		CHECK(rig.current() == QStringLiteral("Mode"));
		rig.key(Qt::Key_Left);
		CHECK(rig.current() == QStringLiteral("Calib"));
		rig.key(Qt::Key_Left);
		CHECK(!rig.row(rig.rowOf(QStringLiteral("Calib"))).value("expanded").toBool());
		// A fold keeps what was open under it, and Right brings it back.
		rig.key(Qt::Key_Asterisk);
		CHECK(rig.call("rowCount").toInt() == 25);
		rig.key(Qt::Key_Left);
		CHECK(rig.call("rowCount").toInt() == 5);
		rig.key(Qt::Key_Right);
		CHECK(rig.call("rowCount").toInt() == 25);
		// Enter toggles a branch; on a leaf it edits; F2 on a branch does nothing.
		rig.call("select", rig.rowOf(QStringLiteral("Phase")));
		rig.key(Qt::Key_Return);
		CHECK(!rig.row(rig.rowOf(QStringLiteral("Phase"))).value("expanded").toBool());
		rig.key(Qt::Key_Return);
		CHECK(rig.row(rig.rowOf(QStringLiteral("Phase"))).value("expanded").toBool());
		rig.key(Qt::Key_F2);
		CHECK(!rig.row(rig.rowOf(QStringLiteral("Phase"))).value("editor").toBool() && rig.call("rowCount").toInt() == 25);
		rig.call("select", rig.rowOf(QStringLiteral("Gain")));
		rig.key(Qt::Key_Return);
		CHECK(rig.row(rig.rowOf(QStringLiteral("Gain"))).value("editor").toBool());
		QTest::keyClick(window, Qt::Key_Escape);
		settle();
		// `*` on a collapsed Calib opens Taps, Phase and the three elements, at most 2,000 rows.
		rig.call("reread");
		rig.call("select", rig.rowOf(QStringLiteral("Calib")));
		rig.key(Qt::Key_Asterisk);
		CHECK(rig.call("rowCount").toInt() == 25 && rig.call("rowCount").toInt() <= 2000);
		// The row menu: Shift+F10 with its three entries; Collapse all restores the defaults.
		rig.key(Qt::Key_F10, Qt::ShiftModifier);
		{
			const auto menu = rig.call("menuInfo").toMap();
			CHECK(menu.value("visible").toBool() && menu.value("count").toInt() == 3);
		}
		QTest::keyClick(window, Qt::Key_Escape);
		settle();
		rig.call("menuTrigger", QStringLiteral("treeMenuCollapseAll"));
		CHECK(rig.call("rowCount").toInt() == 5 && rig.row(0).value("expanded").toBool());

		// 9. Memory: what was opened survives a read, and the current row with it.
		rig.call("select", rig.rowOf(QStringLiteral("Calib")));
		rig.key(Qt::Key_Right);
		rig.call("select", rig.rowOf(QStringLiteral("Phase")));
		rig.key(Qt::Key_Right);
		rig.call("select", rig.rowOfPath(QStringLiteral("Calib.Phase[0]")));
		rig.key(Qt::Key_Right);
		rig.call("select", rig.rowOf(QStringLiteral("Offset")));
		CHECK(rig.call("rowCount").toInt() == 5 + 3 + 3 + 2 && rig.rowOfPath(QStringLiteral("Calib.Phase[0].Offset")) > 0);
		const QStringList before = rig.names();
		rig.call("reread");
		CHECK(rig.names() == before);
		CHECK(rig.current() == QStringLiteral("Offset"));
		// Another Field and back: the defaults.
		rig.call("use", QStringLiteral("other"));
		rig.call("reread");
		rig.call("use", QStringLiteral("field"));
		rig.call("reread");
		CHECK(rig.call("rowCount").toInt() == 5);
		// Folding the root survives a read.
		rig.call("select", 0);
		rig.key(Qt::Key_Left);
		CHECK(rig.call("rowCount").toInt() == 1);
		rig.call("reread");
		CHECK(rig.call("rowCount").toInt() == 1 && !rig.row(0).value("expanded").toBool());
		rig.key(Qt::Key_Right);
		CHECK(rig.call("rowCount").toInt() == 5);
		// Left on Calib then Right brings Phase and [0] back open.
		rig.call("use", QStringLiteral("other"));
		rig.call("reread");
		rig.call("use", QStringLiteral("field"));
		rig.call("reread");
		rig.call("select", rig.rowOf(QStringLiteral("Calib")));
		rig.key(Qt::Key_Asterisk);
		rig.key(Qt::Key_Left);
		CHECK(rig.call("rowCount").toInt() == 5);
		rig.key(Qt::Key_Right);
		CHECK(rig.call("rowCount").toInt() == 25);

		// 10. Unsent: one mark, on the leaf, and on the folded branches above it.
		rig.call("reread");
		rig.call("use", QStringLiteral("other"));
		rig.call("reread");
		rig.call("use", QStringLiteral("field"));
		rig.call("reread");
		rig.call("select", rig.rowOf(QStringLiteral("Calib")));
		rig.key(Qt::Key_Asterisk);
		{
			const int offset = rig.rowOf(QStringLiteral("Offset"));
			CHECK(!rig.row(offset).value("unsentMark").toBool());
			CHECK(rig.call("editOffset", offset).toBool());
			CHECK(rig.row(offset).value("unsentMark").toBool());
			CHECK(!rig.row(rig.rowOf(QStringLiteral("Calib"))).value("unsentMark").toBool());
			CHECK(!rig.row(rig.rowOf(QStringLiteral("Gain"))).value("unsentMark").toBool());
			rig.shot(out, QStringLiteral("details-tree-360-edited.png"));
			// Folding [0] shows the mark on [0]; folding Calib shows it on Calib.
			rig.call("select", rig.rowOfPath(QStringLiteral("Calib.Phase[0]")));
			rig.key(Qt::Key_Left);
			CHECK(rig.row(rig.rowOfPath(QStringLiteral("Calib.Phase[0]"))).value("unsentMark").toBool());
			CHECK(!rig.row(rig.rowOfPath(QStringLiteral("Calib.Phase[1]"))).value("unsentMark").toBool());
			rig.call("select", rig.rowOf(QStringLiteral("Calib")));
			rig.key(Qt::Key_Left);
			const auto folded = rig.row(rig.rowOf(QStringLiteral("Calib")));
			CHECK(folded.value("unsentMark").toBool());
			CHECK(str(folded, "desc").contains(QStringLiteral("1 value edited, not applied")));
			CHECK(!rig.row(0).value("unsentMark").toBool());
			// The folded summary shows the edited value.
			rig.key(Qt::Key_Right);
			rig.shot(out, QStringLiteral("details-tree-560-folded-edited.png"));
		}

		// 9b. The Batch's way: a host's memory scoped by item. Two items of one Field
		// keep their own folds while the panel lives; switching back restores each.
		rig.call("hostScope", QStringLiteral("item:1"));
		rig.call("reread");
		CHECK(rig.call("rowCount").toInt() == 5);
		rig.call("select", rig.rowOf(QStringLiteral("Calib")));
		rig.key(Qt::Key_Right);
		CHECK(rig.call("rowCount").toInt() == 8);
		rig.call("hostScope", QStringLiteral("item:2"));
		rig.call("reread");
		CHECK(rig.call("rowCount").toInt() == 5);
		rig.call("select", rig.rowOf(QStringLiteral("Calib")));
		rig.key(Qt::Key_Asterisk);
		CHECK(rig.call("rowCount").toInt() == 25);
		rig.call("hostScope", QStringLiteral("item:1"));
		rig.call("reread");
		CHECK(rig.call("rowCount").toInt() == 8);
		rig.call("hostScope", QStringLiteral("item:2"));
		rig.call("reread");
		CHECK(rig.call("rowCount").toInt() == 25);
		// Another selection does not forget a host's folds.
		rig.call("use", QStringLiteral("other"));
		rig.call("reread");
		rig.call("hostScope", QStringLiteral("item:2"));
		rig.call("reread");
		CHECK(rig.call("rowCount").toInt() == 25);

		// 11. No horizontal scroll, at any width.
		for (int width : {560, 360, 320, 280}) {
			rig.resizeTo(width);
			CHECK(rig.call("maxContentX").toDouble() == 0);
			CHECK(rig.call("contentWidth").toDouble() <= rig.call("viewWidth").toDouble() + 0.5);
		}
		rig.resizeTo(560);
