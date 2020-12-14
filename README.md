# MouseTouchpadFreeze

由於覺得打扣的時候，筆電前面的滑鼠處控面板很煩，常常掌邊肉不小心碰到，游標就會跑掉。但是我又想去動裝置驅動程式，一來是嫌麻煩，二來是怕自己玩到 BSOD;也不想進BIOS更改裝置配置，因為要改回來的話還得進 BIOS 一次，所以就開發了一個 Filter Driver 來過濾滑鼠裝置與核心之間的 I/O 請求封包(IRP)。雖然不是所有符號裝置都用 IRP 通訊，但是像滑鼠這種 polling 的裝置，九成以上的都是，我們的目的是確保它癱瘓就行，攔截 90% 的資料已經足夠。我的作法是當過濾到系統對此裝置進行寫入的動作時，直接將緩衝區清空，其餘動作正常。

## 環境
* Windows 10
* x86/x64 artitecture
* Synaptics SMBus TouchPad driver
* 作業系統需要關閉驅動簽章認證，或者~~劫持合法軟體的簽章~~

## 操作
在管理員權限下的cmd視窗打
### 創建驅動
```
sc create MouseTouchpadFreeze binPath="<專案路徑>\x64\Release\MouseDriver.sys" type= kernel start= demand
```
### 開啟服務
```
sc start MouseTouchpadFreeze
```
### 停止服務
```
sc stop MouseTouchpadFreeze
```
