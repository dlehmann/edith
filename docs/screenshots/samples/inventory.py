"""A small inventory with prices and a report."""

from dataclasses import dataclass, field


@dataclass
class Item:
    name: str
    price: float
    count: int = 0
    tags: list[str] = field(default_factory=list)

    def total(self) -> float:
        return self.price * self.count


class Inventory:
    def __init__(self):
        self.items: dict[str, Item] = {}

    def add(self, item: Item) -> None:
        if item.name in self.items:
            raise ValueError(f"duplicate item: {item.name}")
        self.items[item.name] = item

    def report(self) -> str:
        lines = [f"{i.name:<12}{i.count:>5} x {i.price:8.2f}" for i in self.items.values()]
        total = sum(i.total() for i in self.items.values())
        return "\n".join(lines + [f"Total: {total:.2f}"])


if __name__ == "__main__":
    inv = Inventory()
    inv.add(Item("keyboard", 49.90, 3, ["input"]))
    inv.add(Item("monitor", 189.00, 2))
    print(inv.report())
