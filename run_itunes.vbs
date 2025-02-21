Set WshShell = CreateObject("WScript.Shell")
Set objWMIService = GetObject("winmgmts:\\.\root\cimv2")
Set colProcessList = objWMIService.ExecQuery("Select * from Win32_Process where Name = 'iTunes.exe'")

If colProcessList.Count = 0 Then
    WshShell.Run """D:\schedulers\run_itunes_script.bat""", 0, False
Else
    WshShell.AppActivate "iTunes"
    WScript.Sleep 100
    If WshShell.AppActivate("iTunes") Then
        WshShell.SendKeys "% "
        WScript.Sleep 20
        WshShell.SendKeys "n"
    Else
        WshShell.SendKeys "% "
        WScript.Sleep 20
        WshShell.SendKeys "x"
        WScript.Sleep 20
        WshShell.SendKeys "% "
        WScript.Sleep 20
        WshShell.SendKeys "r"
    End If
End If
