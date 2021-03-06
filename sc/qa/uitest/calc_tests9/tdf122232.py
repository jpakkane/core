# -*- tab-width: 4; indent-tabs-mode: nil; py-indent-offset: 4 -*-
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
from uitest.framework import UITestCase
from uitest.uihelper.common import get_state_as_dict
from uitest.path import get_srcdir_url
from libreoffice.calc.document import get_sheet_from_doc
from libreoffice.calc.conditional_format import get_conditional_format_from_sheet
from uitest.debug import sleep
from libreoffice.calc.document import get_cell_by_position
from libreoffice.uno.propertyvalue import mkPropertyValues
import org.libreoffice.unotest
import pathlib

def get_url_for_data_file(file_name):
    return pathlib.Path(org.libreoffice.unotest.makeCopyFromTDOC(file_name)).as_uri()

#Bug 122232 - UI: Pressing Enter in a protected sheet doesn't jump to next row in every case

class tdf122232(UITestCase):

    def test_tdf122232_press_Enter_key_jump(self):
        calc_doc = self.ui_test.load_file(get_url_for_data_file("tdf122232.ods"))
        xCalcDoc = self.xUITest.getTopFocusWindow()
        gridwin = xCalcDoc.getChild("grid_window")
        document = self.ui_test.get_component()
        #Start with from C6. Press tabulator to reach G6.
        gridwin.executeAction("SELECT", mkPropertyValues({"CELL": "C6"}))
        gridwin.executeAction("TYPE", mkPropertyValues({"KEYCODE": "RIGHT"}))
        gridwin.executeAction("TYPE", mkPropertyValues({"KEYCODE": "LEFT"}))

        gridwin.executeAction("TYPE", mkPropertyValues({"KEYCODE": "TAB"}))
        gridwin.executeAction("TYPE", mkPropertyValues({"KEYCODE": "TAB"}))
        #Press "Enter".
        gridWinState = get_state_as_dict(gridwin)
        self.assertEqual(gridWinState["CurrentRow"], "5")
        self.assertEqual(gridWinState["CurrentColumn"], "6")
        gridwin.executeAction("TYPE", mkPropertyValues({"KEYCODE": "RETURN"}))
        #Cursor jumps to C29 instead of C7.
        gridWinState = get_state_as_dict(gridwin)
        self.assertEqual(gridWinState["CurrentRow"], "6")
        self.assertEqual(gridWinState["CurrentColumn"], "2")

        self.ui_test.close_doc()

# vim: set shiftwidth=4 softtabstop=4 expandtab:
