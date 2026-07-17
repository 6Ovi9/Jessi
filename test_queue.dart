void main() async {
  Future<void> q = Future.error('first error');
  q = q.whenComplete(() async { print('task 2'); });
  q = q.whenComplete(() async { print('task 3'); });
  try { await q; } catch (e) { print('caught: $e'); }
}
