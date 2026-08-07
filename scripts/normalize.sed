# 変動する出力値を決定論的な比較のために正規化する。
# Linux VM 内で GNU sed と共に check-linux.sh から使用される（`sed -i -f`）。
#
# 変動源:
#   - PID / UID（プロセス実行ごとに変化）
#   - ucontext_t / si_addr からのアドレス（ASLR、アーキテクチャポインタ幅）
#   - CPU 速度に依存するタイマカウント（04 ビジーループ）
#
# サンプルが出力する形式の PID と UID。
s/PID: [0-9][0-9]*/PID: <PID>/g
s/PID=[0-9][0-9]*/PID=<PID>/g
s/si_pid=[0-9][0-9]*/si_pid=<PID>/g
s/si_uid=[0-9][0-9]*/si_uid=<UID>/g
s/ppid=[0-9][0-9]*/ppid=<PID>/g
# 07 は "kill -INT <pid>" / "kill -TERM <pid>" の指示を表示。
s/kill -INT [0-9][0-9]*/kill -INT <PID>/g
s/kill -TERM [0-9][0-9]*/kill -TERM <PID>/g

# 16進アドレス（8桁以上の16進数）— 02 si_addr、08 保存 PC/SP/FP。
s/0x[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]*/0x<ADDR>/g

# 04: VIRTUAL タイマがビジーループ中に N 回発火 — N は CPU 速度に依存。
s/fired [0-9][0-9]* times during busy loop/fired <N> times during busy loop/g
# 04: 1秒の待機中に発火する REAL タイマ回数はスケジューリング境界で変動し得る。
s/During sleep(1): REAL timer fired [0-9][0-9]* time(s)/During sleep(1): REAL timer fired <N> time(s)/g

# 05/09: SIGSTKSZ 値 — libc/アーキテクチャ依存（glibc aarch64=20480、x86_64=8192;
# glibc >=2.34 では実行時評価; musl も異なる）。期待出力がビルド環境の libc に
# 依存しないよう正規化する。"size" はこれらのサンプルの代替スタックコンテキスト
# でのみ出現するため、コンテキストは明確。
s/size [0-9][0-9]* bytes/size <SIGSTKSZ> bytes/g
s/size [0-9][0-9]*$/size <SIGSTKSZ>/g
