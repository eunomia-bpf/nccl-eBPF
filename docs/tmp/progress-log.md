# NCCLPol 开发进度日志

| 时间 | 步骤 | Codex 任务 | 产出文档 | 状态 |
|------|------|-----------|---------|------|
| 2026-03-08 | Phase 0: 编译 NCCL + plugins + nccl-tests + GPU 验证 | codex exec (background b977nugic) | `docs/tmp/phase0-results.md`, `docs/tmp/phase0-logs/` | 完成 ✓ (6/6 步全部成功) |
| 2026-03-08 | Phase 1: 最小 eBPF tuner plugin (llvmbpf 集成) | codex exec (background bszew3rd2) | `docs/tmp/phase1-results.md`, `src/nccl-policy-plugin/`, `src/ebpf-policies/` | 完成 ✓ (P50=33ns, NCCL 加载成功) |
| 2026-03-09 | Phase 1.5: 迁移 bpftime + 3 policies | codex exec (background b4ypeps5e) | `docs/tmp/bpftime-migration-results.md`, 6 new .bpf.c, native_baseline | 完成 ✓ (noop P50=45ns, verifier works) |
| 2026-03-09 | Review #1: 代码质量审查 | codex exec (background b2c2e1iy4) | `docs/tmp/review1-results.md` | 完成 ✓ (6 个高优 + 8 个中优问题) |
| 2026-03-09 | Revise #1: 修复所有 Review 问题 | codex exec (background bezhh3wgt) | `docs/tmp/revise1-results.md` | 完成 ✓ (all strict verifier pass, P50=42-78ns) |
| 2026-03-09 | 性能测试: CPU + GPU 基准 | codex exec (background bz2adthf2) | `docs/tmp/benchmark-results.md`, `docs/tmp/benchmark-latency.csv` | 完成 ✓ (GPU 开销 <0.05us, 可忽略) |
| 2026-03-09 | Review #2: 性能数据分析 | codex exec (background bkku81lom) | `docs/tmp/review2-results.md` | 完成 ✓ (getCollInfo 未被调用是关键gap) |
| 2026-03-09 | Revise #2: 开销分解+热更新+自适应 | codex exec (background btyi37kes) | `docs/tmp/revise2-results.md` | 完成 ✓ (dispatch+41ns, lookup+11ns, reload 0.3us) |
| 2026-03-09 | Phase 3: 安全属性演示 | codex exec (background b9asp7zdt) | `docs/tmp/phase3-safety-results.md` | 完成 ✓ (14/14 verifier, crash对比, 热更新安全) |
| 2026-03-09 | Phase 4: 单GPU 2-rank集成 | codex exec (background b84hfhssz) | `docs/tmp/phase4-results.md` | 完成 ✓ (getCollInfo 495次, cost table bug修复) |
| 2026-03-09 | 文档清理+algo验证 | codex exec (background bshvnzfk6) | `docs/tmp/doc-cleanup-results.md` | 完成 ✓ (algo/proto 10/10匹配) |
| 2026-03-09 | Profiler adapter实现 | codex exec (background bfbp3twfw) | `docs/tmp/profiler-adapter-results.md` | 完成 ✓ (mixed plugin, real telemetry闭环) |
| 2026-03-09 | 论文初稿撰写(中文) | Claude Code 直接撰写 | `docs/paper-draft.md` | 完成 ✓ (9章492行workshop paper) |
| 2026-03-09 | 评估改进: 多comm+竞争+多collective | codex exec (background bogkgk4wg) | `docs/tmp/eval-improvement-results.md` | 完成 ✓ (3个新实验全部PASS) |
| 2026-03-09 | Trace数据集调研 | codex exec (background bqftur1xi) | `docs/tmp/trace-dataset-survey.md` | 完成 ✓ (PARAM/HTA/ASTRA-sim为top 3) |
