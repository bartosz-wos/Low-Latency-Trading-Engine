import mmap
import struct
import time
import os
import xgboost as xgb
import numpy as np
from collections import deque

SHM_NAME = "/dev/shm/shared_memory"
SHM_SIZE = 4096

CONFIDENCE_THRESH = 0.60
FEATURE_NAMES = ['obi', 'momentum_10', 'momentum_50']

FEE_RATE = 0.0002       
TRADE_SIZE = 100.0      
HOLD_PERIOD = 10
MAX_SPREAD_PCT = 0.0005

class PaperTradingState:
    def __init__(self):
        self.position = 0
        self.entry_price = 0.0
        self.balance = 0.0
        self.trades_count = 0
        self.wins = 0
        self.ticks_in_pos = 0

    def update(self, signal, bid, ask):
        if self.position != 0:
            self.ticks_in_pos += 1
            
            should_close = False
            close_reason = ""

            if self.ticks_in_pos >= HOLD_PERIOD:
                should_close = True
                close_reason = "TIME"
            
            elif (self.position == 1 and signal == -1.0) or \
                 (self.position == -1 and signal == 1.0):
                should_close = True
                close_reason = "FLIP"

            if should_close:
                if self.position == 1:
                    self._close_long(bid, reason=close_reason)
                elif self.position == -1:
                    self._close_short(ask, reason=close_reason)
                
                if close_reason == "FLIP":
                    self._try_open(signal, bid, ask)
                return

        if self.position == 0:
            self._try_open(signal, bid, ask)

    def _try_open(self, signal, bid, ask):
        if signal == 0.0: return

        mid = (ask + bid) / 2.0
        spread_pct = (ask - bid) / mid
        
        if spread_pct > MAX_SPREAD_PCT:
            return

        if signal == 1.0:
            self._open_long(ask)
        elif signal == -1.0:
            self._open_short(bid)

    def _open_long(self, price):
        self.position = 1
        self.entry_price = price
        self.ticks_in_pos = 0
        fee = TRADE_SIZE * FEE_RATE
        self.balance -= fee
        print(f"   >>> OPEN LONG  @ {price:.2f} (Fee: -{fee:.4f})")

    def _close_long(self, price, reason="SIG"):
        pnl = ((price - self.entry_price) / self.entry_price) * TRADE_SIZE
        fee = TRADE_SIZE * FEE_RATE
        realized = pnl - fee
        
        self.balance += realized
        self.trades_count += 1
        if realized > 0: self.wins += 1
        self.position = 0
        print(f"   <<< CLOSE LONG @ {price:.2f} [{reason}] | PnL: {realized:+.4f} USD")

    def _open_short(self, price):
        self.position = -1
        self.entry_price = price
        self.ticks_in_pos = 0
        fee = TRADE_SIZE * FEE_RATE
        self.balance -= fee
        print(f"   >>> OPEN SHORT @ {price:.2f} (Fee: -{fee:.4f})")

    def _close_short(self, price, reason="SIG"):
        pnl = ((self.entry_price - price) / self.entry_price) * TRADE_SIZE
        fee = TRADE_SIZE * FEE_RATE
        realized = pnl - fee
        
        self.balance += realized
        self.trades_count += 1
        if realized > 0: self.wins += 1
        self.position = 0
        print(f"   <<< CLOSE SHORT @ {price:.2f} [{reason}] | PnL: {realized:+.4f} USD")

def main():
    print("[Python] (venv) Loading XGBoost...")
    try:
        model = xgb.Booster()
        model.load_model('ml/sniper_v3.json')
        print("[Python] Model loaded!")
    except Exception as e:
        print(f"[ERROR] Could not load model: {e}")
        return

    history = deque(maxlen=51)
    wallet = PaperTradingState()

    print(f"[Python] Waiting for C++ at {SHM_NAME}...")
    while not os.path.exists(SHM_NAME):
        time.sleep(0.1)

    with open(SHM_NAME, "r+b") as f:
        mm = mmap.mmap(f.fileno(), SHM_SIZE)
        
        mm.seek(137)
        mm.write_byte(1)
        print("[Python] Connected! Simulator START.")

        last_seq = 0
        
        try:
            while True:
                curr_seq = struct.unpack_from("Q", mm, 64)[0]
                
                if curr_seq > last_seq:
                    bid_p, ask_p, bid_v, ask_v = struct.unpack_from("dddd", mm, 80)
                    
                    mid_price = (bid_p + ask_p) / 2.0
                    denom = bid_v + ask_v
                    obi = (bid_v - ask_v) / denom if denom > 0 else 0.0
                    
                    history.append(mid_price)
                    
                    if len(history) == 51:
                        mom_10 = mid_price - history[-11]
                        mom_50 = mid_price - history[0]
                        
                        features = np.array([[obi, mom_10, mom_50]])
                        dmat = xgb.DMatrix(features, feature_names=FEATURE_NAMES)
                        preds = model.predict(dmat)

                        if preds.ndim == 2 and preds.shape[1] == 3:
                            probs = preds[0]
                            prob_down, prob_up = probs[0], probs[2]
                        else:
                            prob_down, prob_up = 0.0, 0.0

                        signal = 0.0
                        if prob_up > CONFIDENCE_THRESH: signal = 1.0 
                        elif prob_down > CONFIDENCE_THRESH: signal = -1.0 
                        
                        prev_trades = wallet.trades_count
                        
                        wallet.update(signal, bid_p, ask_p)
                        
                        if signal != 0.0 or wallet.trades_count > prev_trades or wallet.position != 0:
                            
                            unrealized = 0.0
                            if wallet.position == 1:
                                unrealized = ((bid_p - wallet.entry_price) / wallet.entry_price) * TRADE_SIZE
                            elif wallet.position == -1:
                                unrealized = ((wallet.entry_price - ask_p) / wallet.entry_price) * TRADE_SIZE
                            
                            if wallet.trades_count > prev_trades or curr_seq % 10 == 0:
                                pnl_str = f"{wallet.balance:.4f}"
                                print(f"SEQ:{curr_seq} | SIG:{int(signal):+d} | Bal: {pnl_str} | Unr: {unrealized:+.4f} | Tick: {wallet.ticks_in_pos}/{HOLD_PERIOD}")

                        if signal != 0.0:
                            struct.pack_into("d", mm, 128, signal)

                    else:
                        if curr_seq % 1000 == 0:
                            print(f"[Warmup] {len(history)}/51")

                    last_seq = curr_seq
                    
        except KeyboardInterrupt:
            print(f"\n=== SESSION RESULTS ===")
            print(f"Total Trades: {wallet.trades_count}")
            print(f"Final Balance: {wallet.balance:.4f} USD")
            if wallet.trades_count > 0:
                print(f"Win Rate: {(wallet.wins/wallet.trades_count)*100:.2f}%")
            mm.close()

if __name__ == "__main__":
    main()
