class Repo {
  String? get partnerUserId => null;
}
void main() {
  final repo = Repo();
  if (repo.partnerUserId == null || repo.partnerUserId.isEmpty) {
    print("empty");
  }
}
